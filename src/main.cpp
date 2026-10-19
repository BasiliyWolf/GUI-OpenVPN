#include <QtWidgets>
#include <QtNetwork>
#include <QProcess>
#include <QFile>
#include <QDateTime>
#include <QSettings>
#include <QStandardPaths>
#include <QFileDialog>
#include <QTextStream>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QUrl>
#include <memory>
#include <QSaveFile>
#include <QDir>
#include <QFileInfo>
#include <QCloseEvent>
#include <QTemporaryFile>
#include <QHash>
#include <QStyle>
#include <QGroupBox>
#include <QInputDialog>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QNetworkInterface>
#include <QSet>
#include <QDirIterator>
#include <unistd.h>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonObject>

static QStringList lines(const QString &s) { return s.split('\n', Qt::SkipEmptyParts); }
struct Result { int exitCode; QString output; QString error; };
static Result command(const QString &program, const QStringList &args, int timeout=8000) {
    QProcess p; p.start(program,args);
    if (!p.waitForStarted(2000)) return {-1,{},"Не удалось запустить " + program};
    if (!p.waitForFinished(timeout)) { p.kill(); p.waitForFinished(); return {-1,{},"Превышено время ожидания " + program}; }
    return {p.exitCode(), QString::fromUtf8(p.readAllStandardOutput()).trimmed(),QString::fromUtf8(p.readAllStandardError()).trimmed()};
}
static QString field(const QString &line, int index) { auto v=line.split(':'); return index<v.size()?v[index]:QString(); }

struct SessionCredentials {
    QString password;
    QString certificatePassword;
};
class MainWindow : public QMainWindow {
    Q_OBJECT
    QListWidget *profiles=nullptr;
    QLabel *stateLabel=nullptr,*ipLabel=nullptr,*speedLabel=nullptr,*totalsLabel=nullptr,*interfaceLabel=nullptr;
    QString isolationHelper;
    QString externalIPv4="—", vpnIPv4="—", traySpeed="↑ 0 / ↓ 0 KiB/s";
    QString currentStatus="disconnected";
    QCheckBox *autoReconnect=nullptr;
    QSystemTrayIcon *tray=nullptr;
    QNetworkAccessManager network;
    QTimer pollTimer,retryTimer,trayAnimationTimer;
    int trayFrame=0;
    QString language="ru";
    QSettings settings{"BasiliyWolf","GUI OpenVPN"};
    QStringList uuids;
    QString activeUuid, activeIface, lastSelected;
    quint64 previousRx=0,previousTx=0;
    qint64 previousMs=0;
    bool connected=false, actionPending=false, manualStop=false, quitting=false;
    QFile eventLog;
    QString pendingReconnect;
    QHash<QString, SessionCredentials> sessionCredentials;
    std::unique_ptr<QTemporaryFile> runningPasswordFile;
    int retries=0;
    bool promptEveryTime=true;
    QPushButton *connectBtn=nullptr,*disconnectBtn=nullptr;
public:
    MainWindow(){
        setWindowTitle(QStringLiteral("GUI OpenVPN [aka BasiliyWolf]")); resize(650,740); setMinimumSize(530,620);
        language=settings.value("language","ru").toString();
        setWindowIcon(QIcon(":/icons/network98.svg"));
        setStyleSheet("QMainWindow{background:#f5f7fa} QGroupBox{font-weight:600;border:1px solid #c9d2df;border-radius:8px;margin-top:12px;padding:12px;background:#fff} QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 5px} QPushButton{padding:7px 12px;min-height:21px} QListWidget{border:1px solid #cdd5df;border-radius:6px;background:white;padding:5px} QListWidget::item{padding:10px} QLabel{font-size:13px} QTabWidget::pane{border:1px solid #d5dce6;background:white} QTextEdit{background:white}");
        const QString logDir=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(logDir);
        eventLog.setFileName(logDir+"/events.log");
        if (QFileInfo(eventLog).size()>2*1024*1024) {
            QFile::remove(logDir+"/events.log.old");
            QFile::rename(eventLog.fileName(), logDir+"/events.log.old");
        }
        eventLog.open(QIODevice::WriteOnly|QIODevice::Append|QIODevice::Text);
        QFile::setPermissions(eventLog.fileName(),QFileDevice::ReadOwner|QFileDevice::WriteOwner);
        auto *central=new QWidget; setCentralWidget(central);
        auto *base=new QVBoxLayout(central);
        auto *summary=new QFrame;
        summary->setObjectName("connectionSummary");
        summary->setStyleSheet("QFrame#connectionSummary{background:#ffffff;border:1px solid #cdd5df;border-radius:8px;} QFrame#connectionSummary QLabel{border:none;background:transparent;}");
        auto *summaryLayout=new QVBoxLayout(summary);
        summaryLayout->setSpacing(4);
        summaryLayout->setContentsMargins(12,8,12,8);
        stateLabel=new QLabel("Состояние: неизвестно");
        ipLabel=new QLabel("Внешний IPv4: —");
        interfaceLabel=new QLabel("VPN-интерфейс: —");
        speedLabel=new QLabel("Скорость: —");
        totalsLabel=new QLabel("Передано / принято: —");
        summaryLayout->addWidget(stateLabel);
        summaryLayout->addWidget(ipLabel);
        summaryLayout->addWidget(interfaceLabel);
        summaryLayout->addWidget(speedLabel);
        summaryLayout->addWidget(totalsLabel);
        auto *ipRefresh=new QPushButton("Проверить внешний IPv4");
        summaryLayout->addWidget(ipRefresh);
        connect(ipRefresh,&QPushButton::clicked,this,[this]{checkIp();});
        for(auto *label : {stateLabel,ipLabel,interfaceLabel,speedLabel,totalsLabel})
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        base->addWidget(summary);
        auto *journalButton=new QPushButton("Журнал действий…");
        journalButton->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
        base->addWidget(journalButton);
        connect(journalButton,&QPushButton::clicked,this,[this]{eventJournalDialog();});
        auto *top=new QHBoxLayout;
        auto *left=new QVBoxLayout;
        profiles=new QListWidget; profiles->setMinimumWidth(0);
        profiles->setMinimumHeight(90);
        left->addWidget(new QLabel("VPN-профили"));left->addWidget(profiles,1);
        profiles->setIconSize(QSize(24,24));
        auto *btns=new QGridLayout;
        addButton(btns,"Импорт .ovpn",0,0,[this]{importProfile();});
        addButton(btns,"Удалить",0,1,[this]{removeProfile();});
        connectBtn=new QPushButton("▶ Подключиться");btns->addWidget(connectBtn,1,0);connect(connectBtn,&QPushButton::clicked,this,[this]{startVpn();});
        disconnectBtn=new QPushButton("■ Отключиться");btns->addWidget(disconnectBtn,1,1);connect(disconnectBtn,&QPushButton::clicked,this,[this]{stopVpn();});
        addButton(btns,"Переподключить",2,0,[this]{reconnectVpn();});
        addButton(btns,"Редактор профиля",2,1,[this]{editProfile();});
        addButton(btns,"Учетные данные",4,0,[this]{editCredentials();});
        addButton(btns,"Экспорт .ovpn",3,0,[this]{exportProfile();});
        addButton(btns,"Обновить",3,1,[this]{refreshProfiles();});
        left->addLayout(btns);
        autoReconnect=new QCheckBox("Переподключаться автоматически");
        autoReconnect->setChecked(settings.value("reconnect",true).toBool());
        connect(autoReconnect,&QCheckBox::toggled,this,[this](bool on){settings.setValue("reconnect",on);});
        left->addWidget(autoReconnect);
        auto *prefs=new QHBoxLayout;
        addButton(prefs,"Параметры профиля",[this]{profileSettings();});
        addButton(prefs,"Настройки приложения",[this]{appSettings();});
        addButton(left,"Маршруты трафика",[this]{routeSettings();});
        left->addLayout(prefs);
        addButton(left,"Приложения через VPN / Kill Switch…",[this]{isolatedAppsDialog();});
        top->addLayout(left,1);base->addLayout(top,1);
        connect(profiles,&QListWidget::itemSelectionChanged,this,[this]{lastSelected=selectedUuid();});
        tray=new QSystemTrayIcon(this); updateTray("disconnected");
        auto *menu=new QMenu(this);
        menu->addAction("Показать",this,[this]{showNormal();raise();activateWindow();});
        menu->addAction("Подключить",this,[this]{showNormal();startVpn();});
        menu->addAction("Отключить",this,[this]{stopVpn();});
        menu->addAction("Переподключить",this,[this]{reconnectVpn();});
        menu->addAction("Настройки",this,[this]{showNormal();appSettings();});
        menu->addSeparator();menu->addAction("Выход",this,[this]{quitting=true;qApp->quit();});
        tray->setContextMenu(menu);tray->show();
        trayAnimationTimer.setInterval(420);
        connect(&trayAnimationTimer,&QTimer::timeout,this,[this]{trayFrame=(trayFrame+1)%4;updateTray(currentStatus);});
        trayAnimationTimer.start();
        qApp->installEventFilter(this);
        connect(tray,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason why){if(why==QSystemTrayIcon::Trigger || why==QSystemTrayIcon::DoubleClick){showNormal();raise();activateWindow();}});
        pollTimer.setInterval(2500);connect(&pollTimer,&QTimer::timeout,this,&MainWindow::pollStatus);
        retryTimer.setSingleShot(true);connect(&retryTimer,&QTimer::timeout,this,[this]{if (!connected && !manualStop && !actionPending && autoReconnect->isChecked()) startVpn(activeUuid,false);});
        refreshProfiles();pollStatus();pollTimer.start();checkIp();
        QTimer::singleShot(0,this,[this]{translateVisible();});
        log("GUI OpenVPN [aka BasiliyWolf] запущен.");
        if (!QSystemTrayIcon::isSystemTrayAvailable()) log("Системный трей недоступен в текущем окружении.");
    }
protected:
    bool eventFilter(QObject *obj,QEvent *event) override {
        if(event->type()==QEvent::Show && language=="en" && qobject_cast<QWidget*>(obj))
            QTimer::singleShot(0,this,[this]{translateVisible();});
        return QMainWindow::eventFilter(obj,event);
    }
    void closeEvent(QCloseEvent *e) override { if(!quitting && QSystemTrayIcon::isSystemTrayAvailable()){hide();e->ignore();}else QMainWindow::closeEvent(e); }
private:
    template<typename F> void addButton(QGridLayout *layout,const QString &title,int r,int c,F cb){auto *b=new QPushButton(title);layout->addWidget(b,r,c);connect(b,&QPushButton::clicked,this,cb);}
    template<typename L,typename F> void addButton(L *layout,const QString &title,F cb){auto *b=new QPushButton(title);layout->addWidget(b);connect(b,&QPushButton::clicked,this,cb);}
    void log(const QString &s){
        const QString message=QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss ")+s;
        if (eventLog.isOpen()) {eventLog.write((message+"\n").toUtf8());eventLog.flush();}
    }
    QString selectedUuid() const { int i=profiles->currentRow();return (i>=0 && i<uuids.size())?uuids[i]:QString(); }
    void refreshProfiles(){
        QString keep=selectedUuid();if(keep.isEmpty())keep=lastSelected;
        auto r=command("nmcli",{"-t","-f","UUID,TYPE,NAME","connection","show"});
        if(r.exitCode!=0){log("Ошибка nmcli: "+r.error);return;}
        profiles->clear();uuids.clear();
        for(const auto &ln:lines(r.output)){
            QStringList v;QString cur;bool escaped=false;
            for(QChar ch:ln){if(escaped){cur+=ch;escaped=false;}else if(ch=='\\'){escaped=true;}else if(ch==':'){v<<cur;cur.clear();}else cur+=ch;}v<<cur;
            if(v.size()<3 || v[1]!="vpn")continue;
            auto type=command("nmcli",{"-g","vpn.service-type","connection","show",v[0]});
            if(type.exitCode!=0 || !type.output.contains("openvpn",Qt::CaseInsensitive))continue;
            uuids<<v[0]; auto *item=new QListWidgetItem(QIcon(":/icons/network98.svg"),v.mid(2).join(":")); profiles->addItem(item);
        }
        int index=uuids.indexOf(keep);if(index>=0)profiles->setCurrentRow(index);else if(!uuids.isEmpty())profiles->setCurrentRow(0);
    }
    void importProfile(){
        QString file=QFileDialog::getOpenFileName(this,"Импорт OpenVPN",{},"OpenVPN profile (*.ovpn *.conf)");if(file.isEmpty())return;
        auto r=command("nmcli",{"connection","import","type","openvpn","file",file},15000);
        log(r.exitCode==0?"Импорт: "+r.output:"Ошибка импорта: "+r.error);
        if(r.exitCode!=0)QMessageBox::warning(this,"Импорт",r.error);refreshProfiles();
    }
    void removeProfile(){auto id=selectedUuid();if(id.isEmpty())return;
        if(QMessageBox::question(this,"Удаление","Удалить выбранный профиль из NetworkManager?")!=QMessageBox::Yes)return;
        auto r=command("nmcli",{"connection","delete","uuid",id});log(r.exitCode==0?"Профиль удалён":"Ошибка: "+r.error);refreshProfiles();}
    void exportProfile(){auto id=selectedUuid();if(id.isEmpty())return;
        auto path=QFileDialog::getSaveFileName(this,"Экспорт профиля",{},"OpenVPN (*.ovpn)");if(path.isEmpty())return;
        auto r=command("nmcli",{"connection","export",id},15000);
        if(r.exitCode!=0){QMessageBox::warning(this,"Экспорт",r.error);return;}
        QFile f(path);if(!f.open(QIODevice::WriteOnly|QIODevice::NewOnly)){QMessageBox::warning(this,"Экспорт","Не удалось создать файл (или файл уже существует)");return;}
        f.write(r.output.toUtf8());f.write("\n");f.close();QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::WriteOwner);
        log("Профиль экспортирован в "+path+" (проверьте файл перед передачей: возможны ключи и сертификаты).");}
    void editProfile(){auto id=selectedUuid();if(id.isEmpty())return;
        if(!QStandardPaths::findExecutable("nm-connection-editor").isEmpty()){
            if(!QProcess::startDetached("nm-connection-editor",{"--edit",id}))log("Не удалось открыть nm-connection-editor");
        }else QMessageBox::information(this,"Редактор","Установите пакет network-manager-gnome (nm-connection-editor).");}
    QString connectionValue(const QString &id,const QString &key) {
        auto r=command("nmcli",{"-g",key,"connection","show",id});
        return r.exitCode==0?r.output:QString();
    }
    bool secretTool(const QStringList &args, const QByteArray &input, QByteArray *out=nullptr) {
        QProcess proc;
        proc.start("secret-tool",args);
        if(!proc.waitForStarted(2000)) return false;
        if(!input.isEmpty()) proc.write(input);
        proc.closeWriteChannel();
        if(!proc.waitForFinished(10000)) {proc.kill();proc.waitForFinished();return false;}
        if(out) *out=proc.readAllStandardOutput();
        return proc.exitStatus()==QProcess::NormalExit && proc.exitCode()==0;
    }
    QString loadSecret(const QString &id,const QString &kind) {
        QByteArray secret;
        if(secretTool({"lookup","app","gui-openvpn-basiliywolf","uuid",id,"kind",kind},{},&secret))
            return QString::fromUtf8(secret).remove(QRegularExpression("\\n$"));
        return {};
    }
    bool saveSecret(const QString &id,const QString &kind,const QString &value) {
        if(value.isEmpty()) {
            return secretTool({"clear","app","gui-openvpn-basiliywolf","uuid",id,"kind",kind},{});
        }
        QByteArray b=value.toUtf8();
        const bool ok=secretTool({"store","--label=GUI OpenVPN [aka BasiliyWolf] / "+kind,
             "app","gui-openvpn-basiliywolf","uuid",id,"kind",kind},b);
        b.fill(0);
        return ok;
    }
    bool credentialsDialog(const QString &id,bool forConnection){
        QDialog dlg(this);dlg.setWindowTitle(forConnection?"Авторизация VPN — подключение":"Учетные данные VPN");dlg.resize(525,330);
        auto *root=new QVBoxLayout(&dlg);
        auto *headline=new QLabel(forConnection?"Введите учетные данные для подключения":"Изменение учетных данных");
        QFont header=headline->font();header.setPointSize(header.pointSize()+2);header.setBold(true);headline->setFont(header);root->addWidget(headline);
        auto *form=new QFormLayout;
        QLineEdit username(connectionValue(id,"vpn.user-name"));
        if(username.text().isEmpty()) { auto data=connectionValue(id,"vpn.data");QRegularExpression re("username\\s*=\\s*([^,\\n]+)");auto m=re.match(data);if(m.hasMatch())username.setText(m.captured(1).trimmed()); }
        QLineEdit password,certPassword;password.setEchoMode(QLineEdit::Password);certPassword.setEchoMode(QLineEdit::Password);
        auto cached=sessionCredentials.value(id);
        password.setText(cached.password.isEmpty()?loadSecret(id,"password"):cached.password);
        certPassword.setText(cached.certificatePassword.isEmpty()?loadSecret(id,"cert-pass"):cached.certificatePassword);
        form->addRow("Логин:",&username);form->addRow("Пароль пользователя:",&password);form->addRow("Пароль ключа сертификата:",&certPassword);root->addLayout(form);
        QCheckBox reveal("Показать пароли"),remember("Сохранить пароли в системном хранилище (Keyring)");
        const bool keyringAvailable=!QStandardPaths::findExecutable("secret-tool").isEmpty();
        remember.setChecked(keyringAvailable && settings.value("remember/"+id,false).toBool());
        remember.setEnabled(keyringAvailable);
        if(!keyringAvailable) remember.setToolTip("Установите libsecret-tools: sudo apt install libsecret-tools");
        connect(&reveal,&QCheckBox::toggled,&dlg,[&](bool on){password.setEchoMode(on?QLineEdit::Normal:QLineEdit::Password);certPassword.setEchoMode(on?QLineEdit::Normal:QLineEdit::Password);});
        root->addWidget(&reveal);root->addWidget(&remember);
        if(!keyringAvailable) {
            auto *warning=new QLabel("Сохранение паролей недоступно: установите libsecret-tools (sudo apt install libsecret-tools). Пароли будут использоваться только до закрытия программы.");
            warning->setWordWrap(true);
            root->addWidget(warning);
        }
        QLabel *notice=new QLabel("Пароль сертификата нужен только при зашифрованном закрытом ключе.\nСохраненные пароли защищаются системным Keyring; может потребоваться его разблокировка.");notice->setWordWrap(true);root->addWidget(notice);
        auto *buttons=new QDialogButtonBox(forConnection?QDialogButtonBox::Ok|QDialogButtonBox::Cancel:QDialogButtonBox::Save|QDialogButtonBox::Cancel);
        connect(buttons,&QDialogButtonBox::accepted,&dlg,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);root->addWidget(buttons);
        if(dlg.exec()!=QDialog::Accepted)return false;
        const QString login=username.text().trimmed();
        if(login.contains(',')||login.contains('\n')||login.contains('\r')){QMessageBox::warning(this,"Логин","Недопустимые символы в логине.");return false;}
        if(!login.isEmpty()){
            const auto r=command("nmcli",{"connection","modify",id,"+vpn.data","username="+login},12000);
            if(r.exitCode!=0){QMessageBox::warning(this,"Авторизация","Не удалось сохранить логин: "+r.error);return false;}
        }
        sessionCredentials[id]={password.text(),certPassword.text()};
        settings.setValue("remember/"+id,keyringAvailable && remember.isChecked());
        if(remember.isChecked() && keyringAvailable) {
            const bool first=saveSecret(id,"password",password.text());
            const bool second=saveSecret(id,"cert-pass",certPassword.text());
            if(!first||!second) QMessageBox::warning(this,"Keyring","Не удалось сохранить все секреты в Keyring. Проверьте, разблокировано ли хранилище.");
        }else{
            if(!QStandardPaths::findExecutable("secret-tool").isEmpty()) {
                saveSecret(id,"password",{});saveSecret(id,"cert-pass",{});
            }
        }
        log("Учетные данные обновлены (секреты не журналируются).");
        return true;
    }
    void editCredentials(){const auto id=selectedUuid();if(!id.isEmpty())credentialsDialog(id,false);}
    bool prepareSecretFile(const QString &id, QStringList &args){
        runningPasswordFile.reset();
        if(!sessionCredentials.contains(id)) {
            SessionCredentials c; c.password=loadSecret(id,"password");c.certificatePassword=loadSecret(id,"cert-pass");
            sessionCredentials.insert(id,c);
        }
        const SessionCredentials creds=sessionCredentials.value(id);
        if(creds.password.isEmpty() && creds.certificatePassword.isEmpty()) return true;
        auto file=std::make_unique<QTemporaryFile>(QDir::tempPath()+"/ovpn-credentials-XXXXXX");
        file->setAutoRemove(true);
        if(!file->open()) {QMessageBox::critical(this,"Авторизация","Не удалось создать защищённый временный файл.");return false;}
        file->setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
        QByteArray data;
        for(const QString &value:{creds.password,creds.certificatePassword}){
            if(value.contains('\n')||value.contains('\r')){
                QMessageBox::warning(this,"Авторизация","Пароли с переводом строки не поддерживаются."); return false;
            }
        }
        if(!creds.password.isEmpty()) data.append("vpn.secrets.password:"+creds.password.toUtf8()+"\n");
        if(!creds.certificatePassword.isEmpty()) data.append("vpn.secrets.cert-pass:"+creds.certificatePassword.toUtf8()+"\n");
        if(file->write(data)!=data.size() || !file->flush()){
            data.fill(0);QMessageBox::critical(this,"Авторизация","Не удалось подготовить пароль для NetworkManager.");return false;
        }
        data.fill(0);
        args << "passwd-file" << file->fileName();
        runningPasswordFile=std::move(file);
        return true;
    }
    void profileSettings(){
        const QString id=selectedUuid(); if(id.isEmpty()) {QMessageBox::information(this,"Профиль","Выберите профиль.");return;}
        QDialog dlg(this); dlg.setWindowTitle("Параметры VPN-профиля");
        auto *form=new QFormLayout(&dlg);
        QLineEdit name(connectionValue(id,"connection.id"));
        QCheckBox ipv4Default("Не использовать VPN как маршрут по умолчанию (IPv4)");
        ipv4Default.setChecked(connectionValue(id,"ipv4.never-default")=="yes");
        QCheckBox ipv6Default("Не использовать VPN как маршрут по умолчанию (IPv6)");
        ipv6Default.setChecked(connectionValue(id,"ipv6.never-default")=="yes");
        QCheckBox ignoreDNS("Игнорировать автоматически полученные DNS (IPv4)");
        ignoreDNS.setChecked(connectionValue(id,"ipv4.ignore-auto-dns")=="yes");
        form->addRow("Название",&name);
        form->addRow(&ipv4Default);form->addRow(&ipv6Default);form->addRow(&ignoreDNS);
        auto *hint=new QLabel("Маршруты и DNS применятся при следующем подключении.\nЛогин и пароли — кнопка «Авторизация / ключ». Остальные параметры — системный редактор.");
        hint->setWordWrap(true);form->addRow(hint);
        QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
        connect(&buttons,&QDialogButtonBox::accepted,&dlg,&QDialog::accept);
        connect(&buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
        form->addRow(&buttons);
        if(dlg.exec()!=QDialog::Accepted)return;
        if(name.text().trimmed().isEmpty()) {QMessageBox::warning(this,"Профиль","Имя не может быть пустым.");return;}
        auto r=command("nmcli",{"connection","modify",id,
            "connection.id",name.text().trimmed(),
            "ipv4.never-default",ipv4Default.isChecked()?"yes":"no",
            "ipv6.never-default",ipv6Default.isChecked()?"yes":"no",
            "ipv4.ignore-auto-dns",ignoreDNS.isChecked()?"yes":"no"},15000);
        if(r.exitCode!=0){QMessageBox::warning(this,"Профиль",r.error+"\n"+r.output);log("Ошибка обновления профиля: "+r.error);}
        else{log("Изменены параметры профиля "+id);refreshProfiles();}
    }
    static QStringList findOpenVpnBinaries(){
        QStringList found;
        const QStringList dirs={"/usr/sbin","/usr/bin","/sbin","/bin","/usr/local/sbin","/usr/local/bin","/opt"};
        QSet<QString> seen;
        for(const QString &dir:dirs){
            if(dir=="/opt"){
                QDirIterator it(dir,{"openvpn","openvpn2"},QDir::Files|QDir::Executable,QDirIterator::Subdirectories);
                int count=0;
                while(it.hasNext() && count++<1000){auto path=it.next();if(!seen.contains(path)){seen.insert(path);found<<path;}}
            }else{
                QDir d(dir);
                for(const auto &fi:d.entryInfoList({"openvpn","openvpn2"},QDir::Files|QDir::Executable)){
                    auto path=fi.absoluteFilePath();if(!seen.contains(path)){seen.insert(path);found<<path;}
                }
            }
        }
        return found;
    }
    void openVpnVersions(QVBoxLayout *layout){
        auto *group=new QGroupBox("Обнаруженные версии OpenVPN");
        auto *v=new QVBoxLayout(group);
        auto *selector=new QComboBox;
        auto *detail=new QLabel;
        detail->setWordWrap(true);
        v->addWidget(selector);v->addWidget(detail);
        auto *scan=new QPushButton("Повторить поиск");v->addWidget(scan);
        auto discover=[selector,detail](){
            selector->clear();
            for(const auto &path:findOpenVpnBinaries()){
                auto r=command(path,{"--version"},2500);
                const QString version=lines(r.output).value(0);
                if(r.exitCode!=0 || !version.startsWith("OpenVPN ",Qt::CaseInsensitive)) continue;
                selector->addItem(version+" — "+path,path);
            }
            if(selector->count()==0){selector->addItem("OpenVPN не найден");detail->setText("Установите пакет openvpn.");}
        };
        connect(scan,&QPushButton::clicked,group,discover);
        connect(selector,qOverload<int>(&QComboBox::currentIndexChanged),group,[selector,detail](int){
            detail->setText("Выбранный исполняемый файл: "+selector->currentData().toString()+"\n"
                "Важно: NetworkManager сам выбирает бинарный файл OpenVPN. Это список обнаруженных версий, "
                "а не переключатель движка VPN. Для реального выбора альтернативной версии "
                "потребуется отдельный режим запуска OpenVPN вне NetworkManager.");
        });
        discover();
        layout->addWidget(group);
    }
    void routeSettings(){
        const auto id=selectedUuid();
        if(id.isEmpty()){QMessageBox::information(this,"Маршруты","Выберите VPN-профиль.");return;}
        QDialog dlg(this);dlg.setWindowTitle("Маршрутизация VPN — "+connectionValue(id,"connection.id"));dlg.resize(710,620);
        auto *layout=new QVBoxLayout(&dlg);
        auto *mode=new QComboBox;
        mode->addItems({"Весь трафик через VPN (IPv4/IPv6)","Только выбранные сети через VPN", "Маршруты сервера + мои дополнительные"});
        const bool never4=connectionValue(id,"ipv4.never-default")=="yes";
        const bool never6=connectionValue(id,"ipv6.never-default")=="yes";
        const bool ignore4=connectionValue(id,"ipv4.ignore-auto-routes")=="yes";
        const bool ignore6=connectionValue(id,"ipv6.ignore-auto-routes")=="yes";
        mode->setCurrentIndex((ignore4||ignore6)?1:((never4||never6)?2:0));
        auto *form=new QFormLayout;
        form->addRow("Режим маршрутизации:",mode);
        auto *metric=new QSpinBox;metric->setRange(-1,99999);metric->setSpecialValueText("Автоматически");
        const QString currentMetric=connectionValue(id,"ipv4.route-metric");metric->setValue(currentMetric.toInt());
        form->addRow("Метрика IPv4:",metric);layout->addLayout(form);
        auto *r4=new QPlainTextEdit;auto *r6=new QPlainTextEdit;
        r4->setPlaceholderText("192.168.10.0/24\n10.20.0.0/16");
        r6->setPlaceholderText("2001:db8:100::/64");
        auto normalize=[](QString input){input.replace(",","\n");return input.split('\n',Qt::SkipEmptyParts).join("\n");};
        r4->setPlainText(normalize(connectionValue(id,"ipv4.routes")));
        r6->setPlainText(normalize(connectionValue(id,"ipv6.routes")));
        layout->addWidget(new QLabel("Дополнительные маршруты IPv4 (одна сеть CIDR в строке):"));layout->addWidget(r4);
        layout->addWidget(new QLabel("Дополнительные маршруты IPv6 (одна сеть CIDR в строке):"));layout->addWidget(r6);
        auto *info=new QLabel("Для режима «Только выбранные сети» игнорируются маршруты, присланные VPN-сервером. "
            "Локальный и интернет-трафик остаются через обычное соединение, кроме перечисленных сетей. "
            "Другие режимы допускают маршруты, присланные сервером. Изменения действуют после переподключения. "
            "Настройки DNS и IPv6 тоже могут влиять на доступность ресурсов.");
        info->setWordWrap(true);layout->addWidget(info);
        QDialogButtonBox buttons(QDialogButtonBox::Save|QDialogButtonBox::Cancel);
        layout->addWidget(&buttons);
        connect(&buttons,&QDialogButtonBox::accepted,&dlg,&QDialog::accept);
        connect(&buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
        if(dlg.exec()!=QDialog::Accepted)return;
        auto parse=[&](QPlainTextEdit *input,QAbstractSocket::NetworkLayerProtocol proto,QString &result)->bool{
            QStringList cleaned;
            for(const auto &raw:input->toPlainText().split('\n',Qt::SkipEmptyParts)){
                const auto value=raw.trimmed();if(value.isEmpty())continue;
                const auto pair=QHostAddress::parseSubnet(value);
                if(pair.second<0 || pair.first.protocol()!=proto || !value.contains('/')){
                    QMessageBox::warning(this,"Ошибка маршрута","Неверная сеть: "+value+". Укажите сеть в формате CIDR.");return false;
                }
                cleaned<<pair.first.toString()+"/"+QString::number(pair.second);
            }
            result=cleaned.join(", ");return true;
        };
        QString routes4,routes6;
        if(!parse(r4,QAbstractSocket::IPv4Protocol,routes4) || !parse(r6,QAbstractSocket::IPv6Protocol,routes6))return;
        const int i=mode->currentIndex();
        QStringList args={"connection","modify",id,
          "ipv4.never-default",i==0?"no":"yes",
          "ipv6.never-default",i==0?"no":"yes",
          "ipv4.ignore-auto-routes",i==1?"yes":"no",
          "ipv6.ignore-auto-routes",i==1?"yes":"no",
          "ipv4.routes",routes4,
          "ipv6.routes",routes6,
          "ipv4.route-metric",QString::number(metric->value())};
        const auto result=command("nmcli",args,15000);
        if(result.exitCode!=0){QMessageBox::warning(this,"Маршруты","Не удалось сохранить: "+result.error+"\n"+result.output);log("Ошибка сохранения маршрутов: "+result.error);return;}
        log("Обновлена маршрутизация профиля "+id+"; применится после подключения.");
        QMessageBox::information(this,"Маршруты","Маршруты сохранены. Для применения переподключите VPN.");
    }
    void appSettings(){
        QDialog dlg(this);dlg.setWindowTitle("Настройки приложения");
        auto *layout=new QVBoxLayout(&dlg);
        QCheckBox autostart("Запускать GUI при входе в систему");
        auto *languageRow=new QHBoxLayout;
        auto *languageLabel=new QLabel("Язык / Language:");
        auto *languageBox=new QComboBox;
        languageBox->addItem("Русский", "ru");
        languageBox->addItem("English", "en");
        languageBox->setCurrentIndex(language=="en"?1:0);
        languageRow->addWidget(languageLabel);languageRow->addWidget(languageBox);
        layout->addLayout(languageRow);
        const QString autoDir=QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/autostart";
        const QString autoFile=autoDir+"/gui-openvpn-basiliywolf.desktop";
        autostart.setChecked(QFile::exists(autoFile));layout->addWidget(&autostart);
        QLabel note("Автозапуск запускает интерфейс, а не подключение VPN до входа пользователя.\nАвтоматическое переподключение действует при запущенном GUI.");
        note.setWordWrap(true);layout->addWidget(&note);
        openVpnVersions(layout);
        QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
        layout->addWidget(&buttons);
        connect(&buttons,&QDialogButtonBox::accepted,&dlg,&QDialog::accept);
        connect(&buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
        if(dlg.exec()!=QDialog::Accepted)return;
        language=languageBox->currentData().toString();
        settings.setValue("language",language);
        translateVisible();
        pollStatus();
        checkIp();
        if(autostart.isChecked()){
            QDir().mkpath(autoDir);
            QSaveFile f(autoFile);
            if(f.open(QIODevice::WriteOnly|QIODevice::Text)){
                QString exe=QCoreApplication::applicationFilePath();
                exe.replace("\\","\\\\").replace("\"","\\\"").replace("$","\\$").replace("`","\\`");
                const QString desktop="[Desktop Entry]\nType=Application\nName=GUI OpenVPN [aka BasiliyWolf]\nExec=\""+exe+"\"\nIcon=gui-openvpn-basiliywolf\nTerminal=false\nX-GNOME-Autostart-enabled=true\n";
                f.write(desktop.toUtf8());
                if(!f.commit())QMessageBox::warning(this,"Автозапуск","Не удалось сохранить файл автозапуска.");
            }else QMessageBox::warning(this,"Автозапуск","Нет доступа к каталогу автозапуска.");
        }else QFile::remove(autoFile);
    }
    void runAction(const QStringList &args,const QString &label){
        if(actionPending)return;actionPending=true;
        auto *p=new QProcess(this);
        QTimer *timeout=new QTimer(p);timeout->setSingleShot(true);
        connect(timeout,&QTimer::timeout,p,[p]{p->kill();});
        connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,p,label](int code,QProcess::ExitStatus){
            auto msg=QString::fromUtf8(p->readAllStandardOutput()).trimmed();
            auto err=QString::fromUtf8(p->readAllStandardError()).trimmed();
            log(label+": "+(code==0?msg:"ОШИБКА: "+err));runningPasswordFile.reset();actionPending=false;p->deleteLater();
            const QString resume=pendingReconnect;pendingReconnect.clear();
            if(!resume.isEmpty() && !manualStop && code==0){
                QTimer::singleShot(300,this,[this,resume]{startVpn(resume,false);});
            }
            QTimer::singleShot(300,this,&MainWindow::pollStatus);
        });
        connect(p,&QProcess::errorOccurred,this,[this,p](QProcess::ProcessError){log("Ошибка запуска nmcli: "+p->errorString());});
        p->start("nmcli",args);timeout->start(60000);
    }
    void startVpn(QString id={},bool requestAuth=true){if(id.isEmpty())id=selectedUuid();if(id.isEmpty()||actionPending)return;
        if(requestAuth && !credentialsDialog(id,true))return;
        manualStop=false;retryTimer.stop();activeUuid=id;log("Подключение к "+id);
        QStringList args={"--wait","45","connection","up","uuid",id};
        if(!prepareSecretFile(id,args))return;
        runAction(args,"Подключение");}
    void stopVpn(){QString id=activeUuid.isEmpty()?selectedUuid():activeUuid;if(id.isEmpty())return;
        manualStop=true;retryTimer.stop();pendingReconnect.clear();runAction({"--wait","25","connection","down","uuid",id},"Отключение");}
    void reconnectVpn(){
        const QString id=activeUuid.isEmpty()?selectedUuid():activeUuid;
        if(id.isEmpty()||actionPending)return;
        manualStop=false;retryTimer.stop();
        if(connected){pendingReconnect=id;
            runAction({"--wait","25","connection","down","uuid",id},"Переподключение (отключение)");
        }else startVpn(id,false);
    }
    void pollStatus(){
        auto r=command("nmcli",{"-t","-f","UUID,TYPE,DEVICE","connection","show","--active"});if(r.exitCode!=0)return;
        QString uuid,iface;
        for(auto line:lines(r.output)){
            auto v=line.split(':');if(v.size()<3||v[1]!="vpn")continue;
            auto type=command("nmcli",{"-g","vpn.service-type","connection","show",v[0]});
            if(type.output.contains("openvpn",Qt::CaseInsensitive)){uuid=v[0];iface=v.last();break;}
        }
        bool now=!uuid.isEmpty();
        if(now){
            if(!connected || activeUuid!=uuid){log("VPN подключён: "+uuid);checkIp();previousMs=0;retries=0;}
            connected=true;activeUuid=uuid;activeIface=iface;
            vpnIPv4="—";
            for(const auto &entry:QNetworkInterface::allInterfaces()){
                if(entry.name()==iface){for(const auto &a:entry.addressEntries()){
                    if(a.ip().protocol()==QAbstractSocket::IPv4Protocol){vpnIPv4=a.ip().toString();break;}
                }break;}
            }
            stateLabel->setText(language=="en" ? "Status: 🟢 Connected" : "Состояние: 🟢 Подключено");interfaceLabel->setText((language=="en" ? "VPN interface: " : "VPN-интерфейс: ")+iface);updateTray("connected");if(connectBtn)connectBtn->setEnabled(false);if(disconnectBtn)disconnectBtn->setEnabled(true);updateStats();
        }else{
            if(connected){log("VPN-соединение разорвано");checkIp();}
            connected=false;activeIface.clear();vpnIPv4="—";traySpeed="↑ 0 / ↓ 0 KiB/s";previousMs=0;
            stateLabel->setText(language=="en" ? "Status: 🔴 Disconnected" : "Состояние: 🔴 Отключено");interfaceLabel->setText(language=="en" ? "VPN interface: —" : "VPN-интерфейс: —");
            speedLabel->setText(language=="en" ? "Speed: —" : "Скорость: —");totalsLabel->setText(language=="en" ? "Sent / received: —" : "Передано / принято: —");updateTray("disconnected");if(connectBtn)connectBtn->setEnabled(true);if(disconnectBtn)disconnectBtn->setEnabled(false);
            if(!manualStop && !actionPending && !activeUuid.isEmpty() && autoReconnect->isChecked() && !retryTimer.isActive()){
                int delay=qMin(60,5*(1<<qMin(retries++,4)));
                log(QString("Повтор подключения через %1 сек.").arg(delay));retryTimer.start(delay*1000);
            }
        }
    }
    void updateStats(){
        if(activeIface.isEmpty() || !QRegularExpression("^[a-zA-Z0-9_.-]+$").match(activeIface).hasMatch())return;
        QFile rxFile("/sys/class/net/"+activeIface+"/statistics/rx_bytes");
        QFile txFile("/sys/class/net/"+activeIface+"/statistics/tx_bytes");
        if(!rxFile.open(QIODevice::ReadOnly)||!txFile.open(QIODevice::ReadOnly))return;
        auto rx=rxFile.readAll().trimmed().toULongLong();auto tx=txFile.readAll().trimmed().toULongLong();
        auto now=QDateTime::currentMSecsSinceEpoch();
        totalsLabel->setText(QString(language=="en" ? "Sent / received: %1 / %2 MiB" : "Передано / принято: %1 / %2 MiB").arg(tx/1048576.0,0,'f',2).arg(rx/1048576.0,0,'f',2));
        if(previousMs>0&&now>previousMs&&rx>=previousRx&&tx>=previousTx){
            double delta=(now-previousMs)/1000.0;
            traySpeed=QString("↑ %1 / ↓ %2 KiB/s").arg((tx-previousTx)/1024.0/delta,0,'f',1).arg((rx-previousRx)/1024.0/delta,0,'f',1);
            speedLabel->setText((language=="en" ? "Speed: " : "Скорость: ")+traySpeed);
            updateTray("connected");
        }
        previousRx=rx;previousTx=tx;previousMs=now;
    }
    void checkIp(){
        QNetworkRequest req(QUrl("https://api.ipify.org"));req.setTransferTimeout(6000);
        auto *reply=network.get(req);
        connect(reply,&QNetworkReply::finished,this,[this,reply]{
            if(reply->error()==QNetworkReply::NoError){QString ip=QString::fromUtf8(reply->readAll()).trimmed();
                QHostAddress addr;if(addr.setAddress(ip)&&addr.protocol()==QAbstractSocket::IPv4Protocol){ externalIPv4=ip;ipLabel->setText((language=="en" ? "Public IPv4: " : "Внешний IPv4: ")+ip);updateTray(currentStatus);}
                else {externalIPv4="—";ipLabel->setText(language=="en" ? "Public IPv4: invalid response" : "Внешний IPv4: некорректный ответ");updateTray(currentStatus);}
            }else{externalIPv4="—";ipLabel->setText(language=="en" ? "Public IPv4: unavailable" : "Внешний IPv4: недоступен");updateTray(currentStatus);log("Проверка IP: "+reply->errorString());}
            reply->deleteLater();
        });
    }
    void eventJournalDialog(){
        QDialog dlg(this);dlg.setWindowTitle("Журнал действий — GUI OpenVPN [aka BasiliyWolf]");dlg.resize(850,520);
        auto *root=new QVBoxLayout(&dlg);
        QPlainTextEdit viewer;viewer.setReadOnly(true);root->addWidget(&viewer);
        QFile f(eventLog.fileName());if(f.open(QIODevice::ReadOnly|QIODevice::Text)) viewer.setPlainText(QString::fromUtf8(f.readAll()));
        auto *controls=new QHBoxLayout;
        addButton(controls,"Журнал NetworkManager",[this]{showJournal();});
        addButton(controls,"Обновить",[&]{QFile current(eventLog.fileName());if(current.open(QIODevice::ReadOnly|QIODevice::Text))viewer.setPlainText(QString::fromUtf8(current.readAll()));});
        addButton(controls,"Сохранить как…",[&]{
            const auto path=QFileDialog::getSaveFileName(&dlg,"Сохранить журнал",{},"Text (*.txt)");
            if(path.isEmpty())return;
            QSaveFile save(path);
            if(!save.open(QIODevice::WriteOnly)||save.write(viewer.toPlainText().toUtf8())<0||!save.commit())
                QMessageBox::warning(&dlg,"Журнал","Не удалось сохранить журнал.");
        });
        addButton(controls,"Закрыть",[&]{dlg.accept();});root->addLayout(controls);
        dlg.exec();
    }
    void isolatedAppsDialog(){
        const QString helper=QStringLiteral("/usr/libexec/gui-openvpn-basiliywolf/bw-netns");
        QDialog dlg(this);dlg.setWindowTitle("Приложения через VPN — изолированный режим");dlg.resize(760,640);
        auto *root=new QVBoxLayout(&dlg);
        auto *intro=new QLabel("Изолированный VPN работает отдельно от NetworkManager. Программы внутри namespace "
            "не могут передавать IPv4/IPv6 мимо туннеля через собственный сетевой стек. "
            "Это экспериментальный режим; системный Kill Switch для всего компьютера не включается. "
            "Поддерживается отдельный запрос логина, пароля и пароля ключа. Проверяйте сетевую изоляцию перед использованием.");
        intro->setWordWrap(true);root->addWidget(intro);
        QFormLayout *form=new QFormLayout;
        QLineEdit profile(settings.value("isolated/profile").toString()), endpoint(settings.value("isolated/endpoint").toString()),
           app(settings.value("isolated/app").toString());
        QSpinBox port;port.setRange(1,65535);port.setValue(settings.value("isolated/port",1194).toInt());
        QComboBox proto;proto.addItems({"udp","tcp"});proto.setCurrentText(settings.value("isolated/proto","udp").toString());
        QComboBox binary;for(const auto &path:findOpenVpnBinaries()){
            const auto r=command(path,{"--version"},2500);
            const auto version=lines(r.output).value(0);
            if(r.exitCode!=0 || !version.startsWith("OpenVPN ",Qt::CaseInsensitive)) continue;
            binary.addItem(version+" — "+path,path);
        }
        const auto previous=settings.value("isolated/binary").toString();
        for(int i=0;i<binary.count();++i) if(binary.itemData(i).toString()==previous)binary.setCurrentIndex(i);
        auto addBrowse=[&](QLineEdit *field,bool ovpn){
            auto *line=new QWidget;auto *h=new QHBoxLayout(line);h->setContentsMargins(0,0,0,0);
            h->addWidget(field,1);auto *browse=new QPushButton("Обзор…");h->addWidget(browse);
            QObject::connect(browse,&QPushButton::clicked,&dlg,[&,field,ovpn]{
                auto path=QFileDialog::getOpenFileName(&dlg,ovpn?"Выберите .ovpn":"Выберите программу",
                                 {},ovpn?"OpenVPN (*.ovpn *.conf)":"Все файлы (*)");
                if(!path.isEmpty())field->setText(path);
            });return line;
        };
        form->addRow("Файл OpenVPN (*.ovpn):",addBrowse(&profile,true));
        form->addRow("Версия / исполняемый файл:",&binary);
        form->addRow("Адрес VPN-сервера (IPv4):",&endpoint);
        form->addRow("Порт:",&port);form->addRow("Транспорт:",&proto);
        form->addRow("Программа для запуска:",addBrowse(&app,false));root->addLayout(form);
        auto *warning=new QLabel("Внимание: профиль должен быть доверенным и без исполняемых скриптов. "
            "Используется прямой IPv4 endpoint. Авторизация передаётся из временного защищённого файла. Запуск GUI-приложений Wayland/X11 зависит от настроек окружения. "
            "Программы, передающие запросы системным службам через IPC, могут использовать сеть этих служб.");
        warning->setWordWrap(true);root->addWidget(warning);
        QPlainTextEdit output;output.setReadOnly(true);root->addWidget(&output,1);
        auto *actions=new QHBoxLayout;
        auto invoke=[&](const QStringList &args,int timeout){
            if(!QFile::exists(helper)){QMessageBox::critical(&dlg,"Компонент отсутствует","Не найден helper: "+helper);return;}
            const auto r=command("pkexec",QStringList{helper}+args,timeout);
            output.appendPlainText((r.exitCode==0?"OK: ":"Ошибка: ")+r.output+"\n"+r.error);
        };
        addButton(actions,"Запустить VPN",[&]{
            if(profile.text().isEmpty()||binary.currentData().toString().isEmpty()||endpoint.text().isEmpty()){
                QMessageBox::warning(&dlg,"VPN","Укажите конфигурацию, бинарный файл и IPv4 сервера.");return;
            }
            QHostAddress addr;if(!addr.setAddress(endpoint.text())||addr.protocol()!=QAbstractSocket::IPv4Protocol){
                QMessageBox::warning(&dlg,"VPN","Требуется буквальный IPv4 сервера.");return;
            }
            settings.setValue("isolated/profile",profile.text());settings.setValue("isolated/binary",binary.currentData());
            settings.setValue("isolated/endpoint",endpoint.text());settings.setValue("isolated/port",port.value());
            settings.setValue("isolated/proto",proto.currentText());settings.setValue("isolated/app",app.text());
            QDialog auth(&dlg);auth.setWindowTitle("Авторизация изолированного VPN");
            QFormLayout authForm(&auth);
            QLineEdit username, password, keyPassword;
            password.setEchoMode(QLineEdit::Password);keyPassword.setEchoMode(QLineEdit::Password);
            authForm.addRow("Имя пользователя:",&username);
            authForm.addRow("Пароль VPN:",&password);
            authForm.addRow("Пароль закрытого ключа:",&keyPassword);
            auto *authButtons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
            authForm.addRow(authButtons);
            connect(authButtons,&QDialogButtonBox::accepted,&auth,&QDialog::accept);
            connect(authButtons,&QDialogButtonBox::rejected,&auth,&QDialog::reject);
            if(auth.exec()!=QDialog::Accepted)return;
            QStringList params={"start",binary.currentData().toString(),profile.text(),endpoint.text(),QString::number(port.value()),proto.currentText()};
            if(!username.text().isEmpty()||!password.text().isEmpty()||!keyPassword.text().isEmpty()){
                if(username.text().contains('\n')||password.text().contains('\n')||keyPassword.text().contains('\n')){
                    QMessageBox::warning(&dlg,"Авторизация","Переводы строк в полях запрещены.");return;
                }
                const QString runtime=qEnvironmentVariable("XDG_RUNTIME_DIR");
                if(runtime!=QStringLiteral("/run/user/%1").arg(getuid())){
                    QMessageBox::critical(&dlg,"Авторизация","Не найден защищённый XDG_RUNTIME_DIR пользователя.");return;
                }
                QTemporaryFile file(runtime+"/bw-vpn-auth-XXXXXX");
                file.setAutoRemove(true);
                if(!file.open() || !file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)){
                    QMessageBox::critical(&dlg,"Авторизация","Не удаётся создать защищённый файл авторизации.");return;
                }
                const QByteArray bytes=(username.text()+"\n"+password.text()+"\n"+keyPassword.text()+"\n").toUtf8();
                if(file.write(bytes)!=bytes.size() || !file.flush()){
                    QMessageBox::critical(&dlg,"Авторизация","Ошибка записи секретов.");return;
                }
                file.close();
                params << QString::number(getuid()) << file.fileName();
                invoke(params,25000);
                return;
            }
            invoke(params,25000);
        });
        addButton(actions,"Проверить туннель",[&]{invoke({"ready"},12000);});
        addButton(actions,"Запустить программу",[&]{
            const auto path=app.text();
            if(!QFileInfo(path).isExecutable()||!QFileInfo(path).isAbsolute()){
                QMessageBox::warning(&dlg,"Программа","Укажите абсолютный путь к исполняемому файлу.");return;
            }
            invoke({"launch",QString::number(getuid()),qEnvironmentVariable("DISPLAY"),qEnvironmentVariable("WAYLAND_DISPLAY"),path},12000);
        });
        root->addLayout(actions);
        auto *bottom=new QHBoxLayout;
        addButton(bottom,"Состояние",[&]{invoke({"status"},10000);});
        addButton(bottom,"Журнал OpenVPN",[&]{invoke({"logs"},10000);});
        addButton(bottom,"Остановить",[&]{invoke({"stop"},15000);});
        addButton(bottom,"Закрыть",[&]{dlg.accept();});root->addLayout(bottom);
        dlg.exec();
    }
    void showJournal(){auto r=command("journalctl",{"--no-pager","-u","NetworkManager","-n","150","--output=short-iso"},12000);
        auto *dlg=new QDialog(this);dlg->setWindowTitle("Журнал NetworkManager");dlg->resize(950,550);
        auto *layout=new QVBoxLayout(dlg);auto *text=new QPlainTextEdit; text->setReadOnly(true);
        text->setPlainText(r.exitCode==0?r.output:(r.output+"\n"+r.error+"\nДля полного журнала могут понадобиться права доступа."));
        layout->addWidget(text);auto *close=new QPushButton("Закрыть");layout->addWidget(close);connect(close,&QPushButton::clicked,dlg,&QDialog::accept);dlg->exec();dlg->deleteLater();}
    void saveLog(){
        const auto path=QFileDialog::getSaveFileName(this,"Сохранить журнал",{},"Text (*.txt)");
        if(path.isEmpty())return;
        eventLog.flush();
        QFile source(eventLog.fileName());
        if(!source.open(QIODevice::ReadOnly|QIODevice::Text)){
            QMessageBox::warning(this,"Журнал","Не удалось прочитать журнал событий.");return;
        }
        QSaveFile destination(path);
        if(!destination.open(QIODevice::WriteOnly) || destination.write(source.readAll())<0 || !destination.commit()){
            QMessageBox::warning(this,"Журнал","Не удалось сохранить журнал событий.");return;
        }
        log("Журнал сохранён");
    }

    void translateVisible(){
        static const QMap<QString,QString> dictionary = []{
            QMap<QString,QString> d;
            const QByteArray data = QByteArray::fromBase64("eyJHVUkgT3BlblZQTiBbYWthIEJhc2lsaXlXb2xmXSDQt9Cw0L/Rg9GJ0LXQvS4iOiAiR1VJIE9wZW5WUE4gW2FrYSBCYXNpbGl5V29sZl0gc3RhcnRlZC4iLCAiVlBOIElQOiI6ICJWUE4gSVA6IiwgIlZQTi3QuNC90YLQtdGA0YTQtdC50YE6IjogIlZQTiBpbnRlcmZhY2U6IiwgIlZQTi3QuNC90YLQtdGA0YTQtdC50YE6ICI6ICJWUE4gaW50ZXJmYWNlOiAiLCAiVlBOLdC/0YDQvtGE0LjQu9C4IjogIlZQTiBwcm9maWxlcyIsICLQkNCy0YLQvtC30LDQv9GD0YHQuiI6ICJBdXRvc3RhcnQiLCAi0JDQstGC0L7Qt9Cw0L/Rg9GB0Log0LfQsNC/0YPRgdC60LDQtdGCINC40L3RgtC10YDRhNC10LnRgSwg0LAg0L3QtSDQv9C+0LTQutC70Y7Rh9C10L3QuNC1IFZQTiDQtNC+INCy0YXQvtC00LAg0L/QvtC70YzQt9C+0LLQsNGC0LXQu9GPLiI6ICJBdXRvc3RhcnQgbGF1bmNoZXMgdGhlIEdVSSwgbm90IHRoZSBWUE4gYmVmb3JlIHVzZXIgbG9naW4uIiwgItCQ0LLRgtC+0LzQsNGC0LjRh9C10YHQutC+0LUg0L/QtdGA0LXQv9C+0LTQutC70Y7Rh9C10L3QuNC1INC00LXQudGB0YLQstGD0LXRgiDQv9GA0Lgg0LfQsNC/0YPRidC10L3QvdC+0LwgR1VJLiI6ICJBdXRvbWF0aWMgcmVjb25uZWN0aW9uIHJlcXVpcmVzIHRoZSBHVUkgdG8gYmUgcnVubmluZy4iLCAi0JDQstGC0L7RgNC40LfQsNGG0LjRjyI6ICJBdXRoZW50aWNhdGlvbiIsICLQkNCy0YLQvtGA0LjQt9Cw0YbQuNGPIFZQTiDigJQg0L/QvtC00LrQu9GO0YfQtdC90LjQtSI6ICJWUE4gYXV0aGVudGljYXRpb24g4oCUIGNvbm5lY3QiLCAi0JDQstGC0L7RgNC40LfQsNGG0LjRjyDQuNC30L7Qu9C40YDQvtCy0LDQvdC90L7Qs9C+IFZQTiI6ICJJc29sYXRlZCBWUE4gYXV0aGVudGljYXRpb24iLCAi0JDQtNGA0LXRgSBWUE4t0YHQtdGA0LLQtdGA0LAgKElQdjQpOiI6ICJWUE4gc2VydmVyIGFkZHJlc3MgKElQdjQpOiIsICLQktC10YDRgdC40Y8gLyDQuNGB0L/QvtC70L3Rj9C10LzRi9C5INGE0LDQudC7OiI6ICJWZXJzaW9uIC8gZXhlY3V0YWJsZToiLCAi0JLQtdGB0Ywg0YLRgNCw0YTQuNC6INGH0LXRgNC10LcgVlBOIChJUHY0L0lQdjYpIjogIkFsbCB0cmFmZmljIHZpYSBWUE4gKElQdjQvSVB2NikiLCAi0JLQvdC10YjQvdC40LkgSVA6IjogIlB1YmxpYyBJUDoiLCAi0JLQvdC10YjQvdC40LkgSVB2NDoiOiAiUHVibGljIElQdjQ6IiwgItCS0L3QtdGI0L3QuNC5IElQdjQ6ICI6ICJQdWJsaWMgSVB2NDogIiwgItCS0L3QtdGI0L3QuNC5IElQdjQ6INC90LXQtNC+0YHRgtGD0L/QtdC9IjogIlB1YmxpYyBJUHY0OiB1bmF2YWlsYWJsZSIsICLQktC90LXRiNC90LjQuSBJUHY0OiDQvdC10LrQvtGA0YDQtdC60YLQvdGL0Lkg0L7RgtCy0LXRgiI6ICJQdWJsaWMgSVB2NDogaW52YWxpZCByZXNwb25zZSIsICLQktC90LjQvNCw0L3QuNC1IjogIldhcm5pbmciLCAi0JLRi9Cx0LXRgNC40YLQtSAub3ZwbiI6ICJTZWxlY3QgLm92cG4iLCAi0JLRi9Cx0LXRgNC40YLQtSBWUE4t0L/RgNC+0YTQuNC70YwuIjogIlNlbGVjdCBhIFZQTiBwcm9maWxlLiIsICLQktGL0LHQtdGA0LjRgtC1INC/0YDQvtCz0YDQsNC80LzRgyI6ICJTZWxlY3QgcHJvZ3JhbSIsICLQktGL0LHQtdGA0LjRgtC1INC/0YDQvtGE0LjQu9GMLiI6ICJTZWxlY3QgYSBwcm9maWxlLiIsICLQktGL0LHRgNCw0L3QvdGL0Lkg0LjRgdC/0L7Qu9C90Y/QtdC80YvQuSDRhNCw0LnQuzogIjogIlNlbGVjdGVkIGV4ZWN1dGFibGU6ICIsICLQktGL0YXQvtC0IjogIkV4aXQiLCAi0JTQsCI6ICJZZXMiLCAi0JTQvtC/0L7Qu9C90LjRgtC10LvRjNC90YvQtSDQvNCw0YDRiNGA0YPRgtGLIElQdjQgKNC+0LTQvdCwINGB0LXRgtGMIENJRFIg0LIg0YHRgtGA0L7QutC1KToiOiAiQWRkaXRpb25hbCBJUHY0IHJvdXRlcyAoQ0lEUiwgb25lIHBlciBsaW5lKToiLCAi0JTQvtC/0L7Qu9C90LjRgtC10LvRjNC90YvQtSDQvNCw0YDRiNGA0YPRgtGLIElQdjYgKNC+0LTQvdCwINGB0LXRgtGMIENJRFIg0LIg0YHRgtGA0L7QutC1KToiOiAiQWRkaXRpb25hbCBJUHY2IHJvdXRlcyAoQ0lEUiwgb25lIHBlciBsaW5lKToiLCAi0JbRg9GA0L3QsNC7IjogIkxvZyIsICLQltGD0YDQvdCw0LsgTmV0d29ya01hbmFnZXIiOiAiTmV0d29ya01hbmFnZXIgbG9nIiwgItCW0YPRgNC90LDQuyBPcGVuVlBOIjogIk9wZW5WUE4gbG9nIiwgItCW0YPRgNC90LDQuyDQtNC10LnRgdGC0LLQuNC54oCmIjogIkFjdGl2aXR5IGxvZ+KApiIsICLQltGD0YDQvdCw0Lsg0YHQvtGF0YDQsNC90ZHQvSI6ICJMb2cgc2F2ZWQiLCAi0JfQsNC60YDRi9GC0YwiOiAiQ2xvc2UiLCAi0JfQsNC/0YPRgdC60LDRgtGMIEdVSSDQv9GA0Lgg0LLRhdC+0LTQtSDQsiDRgdC40YHRgtC10LzRgyI6ICJMYXVuY2ggR1VJIGF0IGxvZ2luIiwgItCX0LDQv9GD0YHRgtC40YLRjCBWUE4iOiAiU3RhcnQgVlBOIiwgItCX0LDQv9GD0YHRgtC40YLRjCDQv9GA0L7Qs9GA0LDQvNC80YMiOiAiTGF1bmNoIGFwcGxpY2F0aW9uIiwgItCY0LPQvdC+0YDQuNGA0L7QstCw0YLRjCDQsNCy0YLQvtC80LDRgtC40YfQtdGB0LrQuCDQv9C+0LvRg9GH0LXQvdC90YvQtSBETlMgKElQdjQpIjogIklnbm9yZSBhdXRvbWF0aWMgRE5TIChJUHY0KSIsICLQmNC80L/QvtGA0YIiOiAiSW1wb3J0IiwgItCY0LzQv9C+0YDRgiAub3ZwbiI6ICJJbXBvcnQgLm92cG4iLCAi0JjQvNGPINC90LUg0LzQvtC20LXRgiDQsdGL0YLRjCDQv9GD0YHRgtGL0LwuIjogIk5hbWUgY2Fubm90IGJlIGVtcHR5LiIsICLQmNC80Y8g0L/QvtC70YzQt9C+0LLQsNGC0LXQu9GPOiI6ICJVc2VybmFtZToiLCAi0JvQvtCz0LjQvSI6ICJVc2VybmFtZSIsICLQm9C+0LPQuNC9OiI6ICJVc2VybmFtZToiLCAi0JzQsNGA0YjRgNGD0YLRiyI6ICJSb3V0ZXMiLCAi0JzQsNGA0YjRgNGD0YLRiyDQuCBETlMg0L/RgNC40LzQtdC90Y/RgtGB0Y8g0L/RgNC4INGB0LvQtdC00YPRjtGJ0LXQvCDQv9C+0LTQutC70Y7Rh9C10L3QuNC4LiI6ICJSb3V0ZXMgYW5kIEROUyB3aWxsIGFwcGx5IG9uIHRoZSBuZXh0IGNvbm5lY3Rpb24uIiwgItCc0LDRgNGI0YDRg9GC0Ysg0YHQtdGA0LLQtdGA0LAgKyDQvNC+0Lgg0LTQvtC/0L7Qu9C90LjRgtC10LvRjNC90YvQtSI6ICJTZXJ2ZXIgcm91dGVzICsgY3VzdG9tIHJvdXRlcyIsICLQnNCw0YDRiNGA0YPRgtGLINGB0L7RhdGA0LDQvdC10L3Riy4g0JTQu9GPINC/0YDQuNC80LXQvdC10L3QuNGPINC/0LXRgNC10L/QvtC00LrQu9GO0YfQuNGC0LUgVlBOLiI6ICJSb3V0ZXMgc2F2ZWQuIFJlY29ubmVjdCBWUE4gdG8gYXBwbHkuIiwgItCc0LDRgNGI0YDRg9GC0Ysg0YLRgNCw0YTQuNC60LAiOiAiVHJhZmZpYyByb3V0ZXMiLCAi0JzQtdGC0YDQuNC60LAgSVB2NDoiOiAiSVB2NCBtZXRyaWM6IiwgItCd0LDQt9Cy0LDQvdC40LUiOiAiTmFtZSIsICLQndCw0YHRgtGA0L7QudC60LgiOiAiU2V0dGluZ3MiLCAi0J3QsNGB0YLRgNC+0LnQutC4IEROUyDQuCBJUHY2INGC0L7QttC1INC80L7Qs9GD0YIg0LLQu9C40Y/RgtGMINC90LAg0LTQvtGB0YLRg9C/0L3QvtGB0YLRjCDRgNC10YHRg9GA0YHQvtCyLiI6ICJETlMgYW5kIElQdjYgc2V0dGluZ3MgY2FuIGFsc28gYWZmZWN0IHJlc291cmNlIGF2YWlsYWJpbGl0eS4iLCAi0J3QsNGB0YLRgNC+0LnQutC4INC/0YDQuNC70L7QttC10L3QuNGPIjogIkFwcGxpY2F0aW9uIHNldHRpbmdzIiwgItCd0LUg0LjRgdC/0L7Qu9GM0LfQvtCy0LDRgtGMIFZQTiDQutCw0Log0LzQsNGA0YjRgNGD0YIg0L/QviDRg9C80L7Qu9GH0LDQvdC40Y4gKElQdjQpIjogIkRvIG5vdCB1c2UgVlBOIGFzIElQdjQgZGVmYXVsdCByb3V0ZSIsICLQndC1INC40YHQv9C+0LvRjNC30L7QstCw0YLRjCBWUE4g0LrQsNC6INC80LDRgNGI0YDRg9GCINC/0L4g0YPQvNC+0LvRh9Cw0L3QuNGOIChJUHY2KSI6ICJEbyBub3QgdXNlIFZQTiBhcyBJUHY2IGRlZmF1bHQgcm91dGUiLCAi0J3QtSDQvdCw0LnQtNC10L0gaGVscGVyOiAiOiAiUHJpdmlsZWdlZCBoZWxwZXIgbm90IGZvdW5kOiAiLCAi0J3QtSDQvdCw0LnQtNC10L0g0LfQsNGJ0LjRidGR0L3QvdGL0LkgWERHX1JVTlRJTUVfRElSINC/0L7Qu9GM0LfQvtCy0LDRgtC10LvRjy4iOiAiU2VjdXJlIHVzZXIgWERHX1JVTlRJTUVfRElSIHdhcyBub3QgZm91bmQuIiwgItCd0LUg0YPQtNCw0LvQvtGB0Ywg0LfQsNC/0YPRgdGC0LjRgtGMICI6ICJGYWlsZWQgdG8gc3RhcnQgIiwgItCd0LUg0YPQtNCw0LvQvtGB0Ywg0L/QvtC00LPQvtGC0L7QstC40YLRjCDQv9Cw0YDQvtC70Ywg0LTQu9GPIE5ldHdvcmtNYW5hZ2VyLiI6ICJDb3VsZCBub3QgcHJlcGFyZSBjcmVkZW50aWFscyBmb3IgTmV0d29ya01hbmFnZXIuIiwgItCd0LUg0YPQtNCw0LvQvtGB0Ywg0L/RgNC+0YfQuNGC0LDRgtGMINC20YPRgNC90LDQuyDRgdC+0LHRi9GC0LjQuS4iOiAiQ291bGQgbm90IHJlYWQgZXZlbnQgbG9nLiIsICLQndC1INGD0LTQsNC70L7RgdGMINGB0L7Qt9C00LDRgtGMINC30LDRidC40YnRkdC90L3Ri9C5INCy0YDQtdC80LXQvdC90YvQuSDRhNCw0LnQuy4iOiAiQ291bGQgbm90IGNyZWF0ZSBhIHNlY3VyZSB0ZW1wb3JhcnkgZmlsZS4iLCAi0J3QtSDRg9C00LDQu9C+0YHRjCDRgdC+0YXRgNCw0L3QuNGC0Ywg0LLRgdC1INGB0LXQutGA0LXRgtGLINCyIEtleXJpbmcuINCf0YDQvtCy0LXRgNGM0YLQtSwg0YDQsNC30LHQu9C+0LrQuNGA0L7QstCw0L3QviDQu9C4INGF0YDQsNC90LjQu9C40YnQtS4iOiAiQ291bGQgbm90IHNhdmUgYWxsIGNyZWRlbnRpYWxzIHRvIHRoZSBrZXlyaW5nLiBDaGVjayB0aGF0IGl0IGlzIHVubG9ja2VkLiIsICLQndC1INGD0LTQsNC70L7RgdGMINGB0L7RhdGA0LDQvdC40YLRjCDQttGD0YDQvdCw0Lsg0YHQvtCx0YvRgtC40LkuIjogIkNvdWxkIG5vdCBzYXZlIGV2ZW50IGxvZy4iLCAi0J3QtSDRg9C00LDQu9C+0YHRjCDRgdC+0YXRgNCw0L3QuNGC0Ywg0LvQvtCz0LjQvTogIjogIkNvdWxkIG5vdCBzYXZlIHVzZXJuYW1lOiAiLCAi0J3QtSDRg9C00LDQu9C+0YHRjCDRgdC+0YXRgNCw0L3QuNGC0Ywg0YTQsNC50Lsg0LDQstGC0L7Qt9Cw0L/Rg9GB0LrQsC4iOiAiQ291bGQgbm90IHNhdmUgYXV0b3N0YXJ0IGVudHJ5LiIsICLQndC1INGD0LTQsNC70L7RgdGMINGB0L7RhdGA0LDQvdC40YLRjDogIjogIkNvdWxkIG5vdCBzYXZlOiAiLCAi0J3QtSDRg9C00LDRkdGC0YHRjyDRgdC+0LfQtNCw0YLRjCDQt9Cw0YnQuNGJ0ZHQvdC90YvQuSDRhNCw0LnQuyDQsNCy0YLQvtGA0LjQt9Cw0YbQuNC4LiI6ICJVbmFibGUgdG8gY3JlYXRlIGEgc2VjdXJlIGNyZWRlbnRpYWwgZmlsZS4iLCAi0J3QtdCy0LXRgNC90LDRjyDRgdC10YLRjDogIjogIkludmFsaWQgbmV0d29yazogIiwgItCd0LXQtNC+0L/Rg9GB0YLQuNC80YvQtSDRgdC40LzQstC+0LvRiyDQsiDQu9C+0LPQuNC90LUuIjogIkludmFsaWQgY2hhcmFjdGVycyBpbiB1c2VybmFtZS4iLCAi0J3QtdC40LfQstC10YHRgtC90L4iOiAiVW5rbm93biIsICLQndC10YIiOiAiTm8iLCAi0J3QtdGCINC00L7RgdGC0YPQv9CwINC6INC60LDRgtCw0LvQvtCz0YMg0LDQstGC0L7Qt9Cw0L/Rg9GB0LrQsC4iOiAiQXV0b3N0YXJ0IGRpcmVjdG9yeSBpcyBub3Qgd3JpdGFibGUuIiwgItCe0LHQt9C+0YDigKYiOiAiQnJvd3Nl4oCmIiwgItCe0LHQvdCw0YDRg9C20LXQvdC90YvQtSDQstC10YDRgdC40LggT3BlblZQTiI6ICJEZXRlY3RlZCBPcGVuVlBOIHZlcnNpb25zIiwgItCe0LHQvdC+0LLQuNGC0YwiOiAiUmVmcmVzaCIsICLQntGB0YLQsNC90L7QstC40YLRjCI6ICJTdG9wIiwgItCe0YLQutC70Y7Rh9C10L3QuNC1IjogIkRpc2Nvbm5lY3RpbmciLCAi0J7RgtC60LvRjtGH0LXQvdC+IjogIkRpc2Nvbm5lY3RlZCIsICLQntGC0LrQu9GO0YfQuNGC0YwiOiAiRGlzY29ubmVjdCIsICLQntGC0LzQtdC90LAiOiAiQ2FuY2VsIiwgItCe0YLQv9GA0LDQstC70LXQvdC+IjogIlNlbnQiLCAi0J7RiNC40LHQutCwIjogIkVycm9yIiwgItCe0YjQuNCx0LrQsCDQt9Cw0L/QuNGB0Lgg0YHQtdC60YDQtdGC0L7Qsi4iOiAiRmFpbGVkIHRvIHdyaXRlIGNyZWRlbnRpYWxzLiIsICLQntGI0LjQsdC60LAg0L7QsdC90L7QstC70LXQvdC40Y8g0L/RgNC+0YTQuNC70Y86ICI6ICJQcm9maWxlIHVwZGF0ZSBmYWlsZWQ6ICIsICLQntGI0LjQsdC60LAg0YHQvtGF0YDQsNC90LXQvdC40Y8g0LzQsNGA0YjRgNGD0YLQvtCyOiAiOiAiUm91dGUgc2F2ZSBmYWlsZWQ6ICIsICLQn9Cw0YDQsNC80LXRgtGA0YsgVlBOLdC/0YDQvtGE0LjQu9GPIjogIlZQTiBwcm9maWxlIG9wdGlvbnMiLCAi0J/QsNGA0LDQvNC10YLRgNGLINC/0YDQvtGE0LjQu9GPIjogIlByb2ZpbGUgb3B0aW9ucyIsICLQn9Cw0YDQvtC70Lgg0YEg0L/QtdGA0LXQstC+0LTQvtC8INGB0YLRgNC+0LrQuCDQvdC1INC/0L7QtNC00LXRgNC20LjQstCw0Y7RgtGB0Y8uIjogIlBhc3N3b3JkcyBjb250YWluaW5nIG5ld2xpbmVzIGFyZSBub3Qgc3VwcG9ydGVkLiIsICLQn9Cw0YDQvtC70YwgVlBOOiI6ICJWUE4gcGFzc3dvcmQ6IiwgItCf0LDRgNC+0LvRjCDQt9Cw0LrRgNGL0YLQvtCz0L4g0LrQu9GO0YfQsDoiOiAiUHJpdmF0ZSBrZXkgcGFzc3dvcmQ6IiwgItCf0LDRgNC+0LvRjCDQutC70Y7Rh9CwINGB0LXRgNGC0LjRhNC40LrQsNGC0LA6IjogIkNlcnRpZmljYXRlIGtleSBwYXNzd29yZDoiLCAi0J/QsNGA0L7Qu9GMINC/0L7Qu9GM0LfQvtCy0LDRgtC10LvRjzoiOiAiVXNlciBwYXNzd29yZDoiLCAi0J/QtdGA0LXQstC+0LTRiyDRgdGC0YDQvtC6INCyINC/0L7Qu9GP0YUg0LfQsNC/0YDQtdGJ0LXQvdGLLiI6ICJOZXdsaW5lcyBhcmUgbm90IHBlcm1pdHRlZCBpbiB0aGVzZSBmaWVsZHMuIiwgItCf0LXRgNC10LTQsNC90L4gLyDQv9GA0LjQvdGP0YLQvjoiOiAiU2VudCAvIHJlY2VpdmVkOiIsICLQn9C10YDQtdC00LDQvdC+IC8g0L/RgNC40L3Rj9GC0L46ICI6ICJTZW50IC8gcmVjZWl2ZWQ6ICIsICLQn9C10YDQtdC/0L7QtNC60LvRjtGH0LDRgtGM0YHRjyDQsNCy0YLQvtC80LDRgtC40YfQtdGB0LrQuCI6ICJBdXRvIHJlY29ubmVjdCIsICLQn9C10YDQtdC/0L7QtNC60LvRjtGH0LjRgtGMIjogIlJlY29ubmVjdCIsICLQn9C+0LLRgtC+0YDQuNGC0Ywg0L/QvtC40YHQuiI6ICJTZWFyY2ggYWdhaW4iLCAi0J/QvtC00LrQu9GO0YfQtdC90LjQtSI6ICJDb25uZWN0aW9uIiwgItCf0L7QtNC60LvRjtGH0LXQvdC+IjogIkNvbm5lY3RlZCIsICLQn9C+0LTQutC70Y7Rh9C40YLRjCI6ICJDb25uZWN0IiwgItCf0L7QtNC60LvRjtGH0LjRgtGM0YHRjyI6ICJDb25uZWN0IiwgItCf0L7QutCw0LfQsNGC0YwiOiAiU2hvdyIsICLQn9C+0LrQsNC30LDRgtGMINC/0LDRgNC+0LvQuCI6ICJTaG93IHBhc3N3b3JkcyIsICLQn9C+0LvRg9GH0LXQvdC+IjogIlJlY2VpdmVkIiwgItCf0L7RgNGCOiI6ICJQb3J0OiIsICLQn9GA0LXQstGL0YjQtdC90L4g0LLRgNC10LzRjyDQvtC20LjQtNCw0L3QuNGPICI6ICJUaW1lZCBvdXQ6ICIsICLQn9GA0LjQu9C+0LbQtdC90LjRjyDRh9C10YDQtdC3IFZQTiAvIEtpbGwgU3dpdGNo4oCmIjogIkFwcHMgdGhyb3VnaCBWUE4gLyBLaWxsIFN3aXRjaOKApiIsICLQn9GA0LjQu9C+0LbQtdC90LjRjyDRh9C10YDQtdC3IFZQTiDigJQg0LjQt9C+0LvQuNGA0L7QstCw0L3QvdGL0Lkg0YDQtdC20LjQvCI6ICJBcHBzIHRocm91Z2ggVlBOIOKAlCBpc29sYXRlZCBtb2RlIiwgItCf0YDQvtCy0LXRgNC40YLRjCDQstC90LXRiNC90LjQuSBJUHY0IjogIkNoZWNrIHB1YmxpYyBJUHY0IiwgItCf0YDQvtCy0LXRgNC60LAgSVA6ICI6ICJJUCBjaGVjazogIiwgItCf0YDQvtCz0YDQsNC80LzQsCI6ICJBcHBsaWNhdGlvbiIsICLQn9GA0L7RhNC40LvRjCI6ICJQcm9maWxlIiwgItCg0LXQtNCw0LrRgtC+0YAiOiAiRWRpdG9yIiwgItCg0LXQtNCw0LrRgtC+0YAg0L/RgNC+0YTQuNC70Y8iOiAiUHJvZmlsZSBlZGl0b3IiLCAi0KHQuNGB0YLQtdC80L3Ri9C5INGC0YDQtdC5INC90LXQtNC+0YHRgtGD0L/QtdC9INCyINGC0LXQutGD0YnQtdC8INC+0LrRgNGD0LbQtdC90LjQuC4iOiAiU3lzdGVtIHRyYXkgdW5hdmFpbGFibGUgaW4gdGhpcyBkZXNrdG9wIGVudmlyb25tZW50LiIsICLQodC60L7RgNC+0YHRgtGMOiI6ICJTcGVlZDoiLCAi0KHQutC+0YDQvtGB0YLRjDogIjogIlNwZWVkOiAiLCAi0KHQutC+0YDQvtGB0YLRjDog4oCUIjogIlNwZWVkOiDigJQiLCAi0KHQvtGB0YLQvtGP0L3QuNC1OiI6ICJTdGF0dXM6IiwgItCh0L7RgdGC0L7Rj9C90LjQtTog0L3QtdC40LfQstC10YHRgtC90L4iOiAiU3RhdHVzOiBVbmtub3duIiwgItCh0L7RgdGC0L7Rj9C90LjQtTog8J+UtCDQntGC0LrQu9GO0YfQtdC90L4iOiAiU3RhdHVzOiDwn5S0IERpc2Nvbm5lY3RlZCIsICLQodC+0YHRgtC+0Y/QvdC40LU6IPCfn6Ig0J/QvtC00LrQu9GO0YfQtdC90L4iOiAiU3RhdHVzOiDwn5+iIENvbm5lY3RlZCIsICLQodC+0YXRgNCw0L3QtdC90LjQtSI6ICJTYXZlIiwgItCh0L7RhdGA0LDQvdC40YLRjCI6ICJTYXZlIiwgItCh0L7RhdGA0LDQvdC40YLRjCDQttGD0YDQvdCw0LsiOiAiU2F2ZSBsb2ciLCAi0KHQvtGF0YDQsNC90LjRgtGMINC60LDQuuKApiI6ICJTYXZlIGFz4oCmIiwgItCi0YDQtdCx0YPQtdGC0YHRjyDQsdGD0LrQstCw0LvRjNC90YvQuSBJUHY0INGB0LXRgNCy0LXRgNCwLiI6ICJBIGxpdGVyYWwgc2VydmVyIElQdjQgYWRkcmVzcyBpcyByZXF1aXJlZC4iLCAi0KPQtNCw0LvQuNGC0YwiOiAiRGVsZXRlIiwgItCj0LrQsNC20LjRgtC1INCw0LHRgdC+0LvRjtGC0L3Ri9C5INC/0YPRgtGMINC6INC40YHQv9C+0LvQvdGP0LXQvNC+0LzRgyDRhNCw0LnQu9GDLiI6ICJTcGVjaWZ5IGFuIGFic29sdXRlIGV4ZWN1dGFibGUgcGF0aC4iLCAi0KPQutCw0LbQuNGC0LUg0LrQvtC90YTQuNCz0YPRgNCw0YbQuNGOLCDQsdC40L3QsNGA0L3Ri9C5INGE0LDQudC7INC4IElQdjQg0YHQtdGA0LLQtdGA0LAuIjogIlNwZWNpZnkgY29uZmlndXJhdGlvbiwgZXhlY3V0YWJsZSBhbmQgc2VydmVyIElQdjQuIiwgItCj0LrQsNC20LjRgtC1INGB0LXRgtGMINCyINGE0L7RgNC80LDRgtC1IENJRFIuIjogIlVzZSBDSURSIG5ldHdvcmsgZm9ybWF0LiIsICLQo9GB0YLQsNC90L7QstC40YLQtSDQv9Cw0LrQtdGCIG5ldHdvcmstbWFuYWdlci1nbm9tZSAobm0tY29ubmVjdGlvbi1lZGl0b3IpLiI6ICJJbnN0YWxsIG5ldHdvcmstbWFuYWdlci1nbm9tZSAobm0tY29ubmVjdGlvbi1lZGl0b3IpLiIsICLQo9GB0YLQsNC90L7QstC40YLQtSDQv9Cw0LrQtdGCIG9wZW52cG4uIjogIkluc3RhbGwgdGhlIG9wZW52cG4gcGFja2FnZS4iLCAi0KPRh9C10YLQvdGL0LUg0LTQsNC90L3Ri9C1IjogIkNyZWRlbnRpYWxzIiwgItCj0YfQtdGC0L3Ri9C1INC00LDQvdC90YvQtSBWUE4iOiAiVlBOIGNyZWRlbnRpYWxzIiwgItCt0LrRgdC/0L7RgNGCIC5vdnBuIjogIkV4cG9ydCAub3ZwbiIsICLilqAg0J7RgtC60LvRjtGH0LjRgtGM0YHRjyI6ICLilqAgRGlzY29ubmVjdCIsICLilrYg0J/QvtC00LrQu9GO0YfQuNGC0YzRgdGPIjogIuKWtiBDb25uZWN0In0=");
            const auto doc=QJsonDocument::fromJson(data);
            const QJsonObject object=doc.object();
            for(auto it=object.begin();it!=object.end();++it)d.insert(it.key(),it.value().toString());
            return d;
        }();
        auto convert=[this,&dictionary](const QString &input)->QString {
            QString result=input;
            QList<QString> keys=dictionary.keys();
            std::sort(keys.begin(),keys.end(),[](const QString&a,const QString&b){return a.size()>b.size();});
            for(const auto &key:keys){
                const auto translated=dictionary.value(key);
                if(language=="en")result.replace(key,translated,Qt::CaseSensitive);
                else result.replace(translated,key,Qt::CaseSensitive);
            }
            return result;
        };
        for(QWidget *window:QApplication::topLevelWidgets()){
            QList<QWidget*> widgets=window->findChildren<QWidget*>(); widgets.prepend(window);
            for(QWidget *w:widgets){
                if(auto *x=qobject_cast<QLabel*>(w)) x->setText(convert(x->text()));
                else if(auto *x=qobject_cast<QAbstractButton*>(w))x->setText(convert(x->text()));
                else if(auto *x=qobject_cast<QGroupBox*>(w))x->setTitle(convert(x->title()));
                if(auto *x=qobject_cast<QComboBox*>(w)){
                    for(int i=0;i<x->count();++i)x->setItemText(i,convert(x->itemText(i)));
                }
                if(auto *x=qobject_cast<QDialog*>(w))x->setWindowTitle(convert(x->windowTitle()));
                if(auto *x=qobject_cast<QLineEdit*>(w))x->setPlaceholderText(convert(x->placeholderText()));
                if(auto *x=qobject_cast<QTabWidget*>(w)){
                    for(int i=0;i<x->count();++i)x->setTabText(i,convert(x->tabText(i)));
                }
                if(auto *x=qobject_cast<QListWidget*>(w)){
                    for(int i=0;i<x->count();++i){auto *item=x->item(i);if(item && item->data(Qt::UserRole).isNull())item->setText(convert(item->text()));}
                }
                for(auto *a:w->actions())a->setText(convert(a->text()));
            }
        }
        if(tray && tray->contextMenu()){for(auto *a:tray->contextMenu()->actions())a->setText(convert(a->text()));}
        updateTray(currentStatus);
    }

    void updateTray(const QString &status){
        currentStatus=status;
        QPixmap px(32,32);px.fill(Qt::transparent);QPainter painter(&px);
        painter.setRenderHint(QPainter::Antialiasing,false);
        painter.setPen(QPen(QColor("#202020"),1));painter.setBrush(QColor("#c0c0c0"));painter.drawRect(2,3,20,16);
        painter.setBrush(QColor("#001f82"));painter.drawRect(4,5,16,12);
        painter.setPen(QPen(Qt::white,1));painter.drawLine(7,8,13,8);painter.drawLine(7,10,12,10);
        painter.setPen(QPen(QColor("#202020"),1));painter.setBrush(QColor("#c0c0c0"));painter.drawRect(9,19,7,3);painter.drawRect(5,22,16,3);
        const bool online=status=="connected";
        const QColor bulb=online ? (trayFrame%2==0?QColor("#00d334"):QColor("#7ef997")) : QColor("#da2525");
        painter.setBrush(bulb);painter.drawEllipse(19,18,11,11);
        if(online){
            painter.setPen(QPen(QColor("#1c69e8"),2));
            const int shift=trayFrame%3;
            painter.drawLine(21+shift,9,25+shift,9);
            painter.drawLine(21+shift,12,25+shift,12);
        }
        painter.end();
        if(tray){tray->setIcon(QIcon(px));
          const bool en=language=="en";
          tray->setToolTip(QString("GUI OpenVPN [aka BasiliyWolf]\n%1\n%2: %3\nVPN IP: %4\n%5")
          .arg(status=="connected"?(en?"Connected":"Подключено"):(en?"Disconnected":"Отключено"),
               en?"Public IP":"Внешний IP",externalIPv4,status=="connected"?vpnIPv4:"—",
               status=="connected"?traySpeed:(en?"Speed: —":"Скорость: —")));}
    }
};
int main(int argc,char **argv){QApplication app(argc,argv); QLocale::setDefault(QLocale::system());app.setApplicationName("GUI OpenVPN [aka BasiliyWolf]");app.setQuitOnLastWindowClosed(false);MainWindow window;window.show();return app.exec();}
#include "main.moc"

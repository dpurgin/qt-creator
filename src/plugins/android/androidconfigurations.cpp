/**************************************************************************
**
** Copyright (c) 2014 BogDan Vatra <bog_dan_ro@yahoo.com>
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "androidconfigurations.h"
#include "androidconstants.h"
#include "androidtoolchain.h"
#include "androiddevice.h"
#include "androidgdbserverkitinformation.h"
#include "ui_addnewavddialog.h"
#include "androidqtversion.h"
#include "androiddevicedialog.h"

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>
#include <utils/hostosinfo.h>
#include <utils/persistentsettings.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/toolchainmanager.h>
#include <projectexplorer/session.h>
#include <debugger/debuggeritemmanager.h>
#include <debugger/debuggerkitinformation.h>
#include <qtsupport/baseqtversion.h>
#include <qtsupport/qtkitinformation.h>
#include <qtsupport/qtversionmanager.h>
#include <utils/environment.h>
#include <utils/sleep.h>

#include <QDateTime>
#include <QSettings>
#include <QStringList>
#include <QProcess>
#include <QFileInfo>
#include <QDirIterator>
#include <QMetaObject>

#include <QStringListModel>
#include <QMessageBox>

using namespace ProjectExplorer;
using namespace Utils;

namespace Android {
namespace Internal {

namespace {
    const QLatin1String SettingsGroup("AndroidConfigurations");
    const QLatin1String SDKLocationKey("SDKLocation");
    const QLatin1String NDKLocationKey("NDKLocation");
    const QLatin1String NDKToolchainVersionKey("NDKToolchainVersion");
    const QLatin1String AntLocationKey("AntLocation");
    const QLatin1String OpenJDKLocationKey("OpenJDKLocation");
    const QLatin1String KeystoreLocationKey("KeystoreLocation");
    const QLatin1String AutomaticKitCreationKey("AutomatiKitCreation");
    const QLatin1String MakeExtraSearchDirectory("MakeExtraSearchDirectory");
    const QLatin1String DefaultDevice("DefaultDevice");
    const QLatin1String PartitionSizeKey("PartitionSize");
    const QLatin1String ToolchainHostKey("ToolchainHost");
    const QLatin1String ArmToolchainPrefix("arm-linux-androideabi");
    const QLatin1String X86ToolchainPrefix("x86");
    const QLatin1String MipsToolchainPrefix("mipsel-linux-android");
    const QLatin1String ArmToolsPrefix("arm-linux-androideabi");
    const QLatin1String X86ToolsPrefix("i686-linux-android");
    const QLatin1String MipsToolsPrefix("mipsel-linux-android");
    const QLatin1String Unknown("unknown");
    const QLatin1String keytoolName("keytool");
    const QLatin1String jarsignerName("jarsigner");
    const QLatin1String changeTimeStamp("ChangeTimeStamp");

    static QString sdkSettingsFileName()
    {
        return QFileInfo(Core::ICore::settings(QSettings::SystemScope)->fileName()).absolutePath()
                + QLatin1String("/qtcreator/android.xml");
    }

    bool androidDevicesLessThan(const AndroidDeviceInfo &dev1, const AndroidDeviceInfo &dev2)
    {
        if (dev1.serialNumber.contains(QLatin1String("????")) == dev2.serialNumber.contains(QLatin1String("????")))
            return !dev1.serialNumber.contains(QLatin1String("????"));
        if (dev1.type != dev2.type)
            return dev1.type == AndroidDeviceInfo::Hardware;
        if (dev1.sdk != dev2.sdk)
            return dev1.sdk < dev2.sdk;

        return dev1.serialNumber < dev2.serialNumber;
    }
}

Abi::Architecture AndroidConfigurations::architectureForToolChainPrefix(const QString& toolchainprefix)
{
    if (toolchainprefix == ArmToolchainPrefix)
        return Abi::ArmArchitecture;
    if (toolchainprefix == X86ToolchainPrefix)
        return Abi::X86Architecture;
    if (toolchainprefix == MipsToolchainPrefix)
        return Abi::MipsArchitecture;
    return Abi::UnknownArchitecture;
}

QLatin1String AndroidConfigurations::toolchainPrefix(Abi::Architecture architecture)
{
    switch (architecture) {
    case Abi::ArmArchitecture:
        return ArmToolchainPrefix;
    case Abi::X86Architecture:
        return X86ToolchainPrefix;
    case Abi::MipsArchitecture:
        return MipsToolchainPrefix;
    default:
        return Unknown;
    }
}

QLatin1String AndroidConfigurations::toolsPrefix(Abi::Architecture architecture)
{
    switch (architecture) {
    case Abi::ArmArchitecture:
        return ArmToolsPrefix;
    case Abi::X86Architecture:
        return X86ToolsPrefix;
    case Abi::MipsArchitecture:
        return MipsToolsPrefix;
    default:
        return Unknown;
    }
}

AndroidConfig::AndroidConfig(const QSettings &settings)
{
    // user settings
    partitionSize = settings.value(PartitionSizeKey, 1024).toInt();
    sdkLocation = FileName::fromString(settings.value(SDKLocationKey).toString());
    ndkLocation = FileName::fromString(settings.value(NDKLocationKey).toString());
    antLocation = FileName::fromString(settings.value(AntLocationKey).toString());
    openJDKLocation = FileName::fromString(settings.value(OpenJDKLocationKey).toString());
    keystoreLocation = FileName::fromString(settings.value(KeystoreLocationKey).toString());
    toolchainHost = settings.value(ToolchainHostKey).toString();
    automaticKitCreation = settings.value(AutomaticKitCreationKey, true).toBool();
    QString extraDirectory = settings.value(MakeExtraSearchDirectory).toString();
    makeExtraSearchDirectories.clear();
    if (!extraDirectory.isEmpty())
        makeExtraSearchDirectories << extraDirectory;

    PersistentSettingsReader reader;
    if (reader.load(FileName::fromString(sdkSettingsFileName()))
            && settings.value(changeTimeStamp).toInt() != QFileInfo(sdkSettingsFileName()).lastModified().toMSecsSinceEpoch() / 1000) {
        // persisten settings
        sdkLocation = FileName::fromString(reader.restoreValue(SDKLocationKey).toString());
        ndkLocation = FileName::fromString(reader.restoreValue(NDKLocationKey).toString());
        antLocation = FileName::fromString(reader.restoreValue(AntLocationKey).toString());
        openJDKLocation = FileName::fromString(reader.restoreValue(OpenJDKLocationKey).toString());
        keystoreLocation = FileName::fromString(reader.restoreValue(KeystoreLocationKey).toString());
        toolchainHost = reader.restoreValue(ToolchainHostKey).toString();
        QVariant v = reader.restoreValue(AutomaticKitCreationKey);
        if (v.isValid())
            automaticKitCreation = v.toBool();
        QString extraDirectory = reader.restoreValue(MakeExtraSearchDirectory).toString();
        makeExtraSearchDirectories.clear();
        if (!extraDirectory.isEmpty())
            makeExtraSearchDirectories << extraDirectory;
        // persistent settings
    }

}

AndroidConfig::AndroidConfig()
{
    partitionSize = 1024;
}

void AndroidConfig::save(QSettings &settings) const
{
    QFileInfo fileInfo(sdkSettingsFileName());
    if (fileInfo.exists())
        settings.setValue(changeTimeStamp, fileInfo.lastModified().toMSecsSinceEpoch() / 1000);

    // user settings
    settings.setValue(SDKLocationKey, sdkLocation.toString());
    settings.setValue(NDKLocationKey, ndkLocation.toString());
    settings.setValue(AntLocationKey, antLocation.toString());
    settings.setValue(OpenJDKLocationKey, openJDKLocation.toString());
    settings.setValue(KeystoreLocationKey, keystoreLocation.toString());
    settings.setValue(PartitionSizeKey, partitionSize);
    settings.setValue(AutomaticKitCreationKey, automaticKitCreation);
    settings.setValue(ToolchainHostKey, toolchainHost);
    settings.setValue(MakeExtraSearchDirectory,
                      makeExtraSearchDirectories.isEmpty() ? QString()
                                                           : makeExtraSearchDirectories.at(0));
}

void AndroidConfigurations::setConfig(const AndroidConfig &devConfigs)
{
    m_config = devConfigs;

    if (m_config.toolchainHost.isEmpty())
        detectToolchainHost();

    save();
    updateAvailableNdkPlatforms();
    updateAvailableSdkPlatforms();
    updateAutomaticKitList();
    updateAndroidDevice();
    emit updated();
}

void AndroidConfigurations::updateAvailableNdkPlatforms()
{
    m_availableNdkPlatforms.clear();
    FileName path = m_config.ndkLocation;
    QDirIterator it(path.appendPath(QLatin1String("platforms")).toString(), QStringList() << QLatin1String("android-*"), QDir::Dirs);
    while (it.hasNext()) {
        const QString &fileName = it.next();
        m_availableNdkPlatforms.push_back(fileName.mid(fileName.lastIndexOf(QLatin1Char('-')) + 1).toInt());
    }
    qSort(m_availableNdkPlatforms.begin(), m_availableNdkPlatforms.end(), qGreater<int>());
}

void AndroidConfigurations::updateAvailableSdkPlatforms()
{
    m_availableSdkPlatforms.clear();

    QProcess proc;
    proc.setProcessEnvironment(androidToolEnvironment().toProcessEnvironment());
    proc.start(androidToolPath().toString(), QStringList() << QLatin1String("list") << QLatin1String("target")); // list avaialbe AVDs
    if (!proc.waitForFinished(5000)) {
        proc.terminate();
        return;
    }
    while (proc.canReadLine()) {
        const QString line = QString::fromLocal8Bit(proc.readLine().trimmed());
        int index = line.indexOf(QLatin1String("\"android-"));
        if (index == -1)
            continue;
        QString androidTarget = line.mid(index + 1, line.length() - index - 2);
        int apiLevel = androidTarget.mid(androidTarget.lastIndexOf(QLatin1Char('-')) + 1).toInt();
        QVector<int>::iterator it = qLowerBound(m_availableSdkPlatforms.begin(), m_availableSdkPlatforms.end(), apiLevel, qGreater<int>());
        m_availableSdkPlatforms.insert(it, apiLevel);
    }
}

QStringList AndroidConfigurations::sdkTargets(int minApiLevel) const
{
    QStringList result;
    for (int i = 0; i < m_availableSdkPlatforms.size(); ++i) {
        if (m_availableSdkPlatforms.at(i) >= minApiLevel)
            result << QLatin1String("android-") + QString::number(m_availableSdkPlatforms.at(i));
        else
            break;
    }
    return result;
}

FileName AndroidConfigurations::adbToolPath() const
{
    FileName path = m_config.sdkLocation;
    return path.appendPath(QLatin1String("platform-tools/adb" QTC_HOST_EXE_SUFFIX));
}

Utils::Environment AndroidConfigurations::androidToolEnvironment() const
{
    Utils::Environment env = Utils::Environment::systemEnvironment();
    if (!m_config.openJDKLocation.isEmpty())
        env.set(QLatin1String("JAVA_HOME"), m_config.openJDKLocation.toUserOutput());
    return env;
}

FileName AndroidConfigurations::androidToolPath() const
{
    if (HostOsInfo::isWindowsHost()) {
        // I want to switch from using android.bat to using an executable. All it really does is call
        // Java and I've made some progress on it. So if android.exe exists, return that instead.
        FileName path = m_config.sdkLocation;
        path.appendPath(QLatin1String("tools/android" QTC_HOST_EXE_SUFFIX));
        if (path.toFileInfo().exists())
            return path;
        path = m_config.sdkLocation;
        return path.appendPath(QLatin1String("tools/android" ANDROID_BAT_SUFFIX));
    } else {
        FileName path = m_config.sdkLocation;
        return path.appendPath(QLatin1String("tools/android"));
    }
}

FileName AndroidConfigurations::antToolPath() const
{
    if (!m_config.antLocation.isEmpty())
        return m_config.antLocation;
    else
        return FileName::fromString(QLatin1String("ant"));
}

FileName AndroidConfigurations::emulatorToolPath() const
{
    FileName path = m_config.sdkLocation;
    return path.appendPath(QLatin1String("tools/emulator" QTC_HOST_EXE_SUFFIX));
}

FileName AndroidConfigurations::toolPath(Abi::Architecture architecture, const QString &ndkToolChainVersion) const
{
    FileName path = m_config.ndkLocation;
    return path.appendPath(QString::fromLatin1("toolchains/%1-%2/prebuilt/%3/bin/%4")
            .arg(toolchainPrefix(architecture))
            .arg(ndkToolChainVersion)
            .arg(m_config.toolchainHost)
            .arg(toolsPrefix(architecture)));
}

FileName AndroidConfigurations::stripPath(Abi::Architecture architecture, const QString &ndkToolChainVersion) const
{
    return toolPath(architecture, ndkToolChainVersion).appendString(QLatin1String("-strip" QTC_HOST_EXE_SUFFIX));
}

FileName AndroidConfigurations::readelfPath(Abi::Architecture architecture, const QString &ndkToolChainVersion) const
{
    return toolPath(architecture, ndkToolChainVersion).appendString(QLatin1String("-readelf" QTC_HOST_EXE_SUFFIX));
}

FileName AndroidConfigurations::gccPath(Abi::Architecture architecture, const QString &ndkToolChainVersion) const
{
    return toolPath(architecture, ndkToolChainVersion).appendString(QLatin1String("-gcc" QTC_HOST_EXE_SUFFIX));
}

FileName AndroidConfigurations::gdbPath(Abi::Architecture architecture, const QString &ndkToolChainVersion) const
{
    return toolPath(architecture, ndkToolChainVersion).appendString(QLatin1String("-gdb" QTC_HOST_EXE_SUFFIX));
}

FileName AndroidConfigurations::openJDKPath() const
{
    return m_config.openJDKLocation;
}

void AndroidConfigurations::detectToolchainHost()
{
    QStringList hostPatterns;
    switch (HostOsInfo::hostOs()) {
    case OsTypeLinux:
        hostPatterns << QLatin1String("linux*");
        break;
    case OsTypeWindows:
        hostPatterns << QLatin1String("windows*");
        break;
    case OsTypeMac:
        hostPatterns << QLatin1String("darwin*");
        break;
    default: /* unknown host */ return;
    }

    FileName path = m_config.ndkLocation;
    QDirIterator it(path.appendPath(QLatin1String("prebuilt")).toString(), hostPatterns, QDir::Dirs);
    if (it.hasNext()) {
        it.next();
        m_config.toolchainHost = it.fileName();
    }
}

FileName AndroidConfigurations::openJDKBinPath() const
{
    FileName path = m_config.openJDKLocation;
    if (!path.isEmpty())
        return path.appendPath(QLatin1String("bin"));
    return path;
}

FileName AndroidConfigurations::keytoolPath() const
{
    return openJDKBinPath().appendPath(keytoolName);
}

FileName AndroidConfigurations::jarsignerPath() const
{
    return openJDKBinPath().appendPath(jarsignerName);
}

FileName AndroidConfigurations::zipalignPath() const
{
    FileName path = m_config.sdkLocation;
    return path.appendPath(QLatin1String("tools/zipalign" QTC_HOST_EXE_SUFFIX));
}

AndroidDeviceInfo AndroidConfigurations::showDeviceDialog(ProjectExplorer::Project *project, int apiLevel, const QString &abi)
{
    QString serialNumber = defaultDevice(project, abi);
    if (!serialNumber.isEmpty()) {
        // search for that device
        foreach (const AndroidDeviceInfo &info, AndroidConfigurations::instance().connectedDevices())
            if (info.serialNumber == serialNumber
                    && info.sdk >= apiLevel)
                return info;

        foreach (const AndroidDeviceInfo &info, AndroidConfigurations::instance().androidVirtualDevices())
            if (info.serialNumber == serialNumber
                    && info.sdk >= apiLevel)
                return info;
    }

    AndroidDeviceDialog dialog(apiLevel, abi, Core::ICore::mainWindow());
    if (dialog.exec() == QDialog::Accepted) {
        AndroidDeviceInfo info = dialog.device();
        if (dialog.saveDeviceSelection()) {
            if (!info.serialNumber.isEmpty())
                AndroidConfigurations::instance().setDefaultDevice(project, abi, info.serialNumber);
        }
        return info;
    }
    return AndroidDeviceInfo();
}

void AndroidConfigurations::clearDefaultDevices(ProjectExplorer::Project *project)
{
    if (m_defaultDeviceForAbi.contains(project))
        m_defaultDeviceForAbi.remove(project);
}

void AndroidConfigurations::setDefaultDevice(ProjectExplorer::Project *project, const QString &abi, const QString &serialNumber)
{
    m_defaultDeviceForAbi[project][abi] = serialNumber;
}

QString AndroidConfigurations::defaultDevice(Project *project, const QString &abi) const
{
    if (!m_defaultDeviceForAbi.contains(project))
        return QString();
    const QMap<QString, QString> &map = m_defaultDeviceForAbi.value(project);
    if (!map.contains(abi))
        return QString();
    return map.value(abi);
}

QVector<AndroidDeviceInfo> AndroidConfigurations::connectedDevices(QString *error) const
{
    QVector<AndroidDeviceInfo> devices;
    QProcess adbProc;
    adbProc.start(adbToolPath().toString(), QStringList() << QLatin1String("devices"));
    if (!adbProc.waitForFinished(5000)) {
        adbProc.kill();
        if (error)
            *error = tr("Could not run: %1").arg(adbToolPath().toString() + QLatin1String(" devices"));
        return devices;
    }
    QList<QByteArray> adbDevs = adbProc.readAll().trimmed().split('\n');
    if (adbDevs.empty())
        return devices;

    while (adbDevs.first().startsWith("* daemon"))
        adbDevs.removeFirst(); // remove the daemon logs
    adbDevs.removeFirst(); // remove "List of devices attached" header line

    // workaround for '????????????' serial numbers:
    // can use "adb -d" when only one usb device attached
    foreach (const QByteArray &device, adbDevs) {
        const QString serialNo = QString::fromLatin1(device.left(device.indexOf('\t')).trimmed());
        const QString deviceType = QString::fromLatin1(device.mid(device.indexOf('\t'))).trimmed();
        if (isBootToQt(serialNo))
            continue;
        AndroidDeviceInfo dev;
        dev.serialNumber = serialNo;
        dev.type = serialNo.startsWith(QLatin1String("emulator")) ? AndroidDeviceInfo::Emulator : AndroidDeviceInfo::Hardware;
        dev.sdk = getSDKVersion(dev.serialNumber);
        dev.cpuAbi = getAbis(dev.serialNumber);
        dev.unauthorized = (deviceType == QLatin1String("unauthorized"));
        devices.push_back(dev);
    }

    qSort(devices.begin(), devices.end(), androidDevicesLessThan);
    if (devices.isEmpty() && error)
        *error = tr("No devices found in output of: %1").arg(adbToolPath().toString() + QLatin1String(" devices"));
    return devices;
}

QString AndroidConfigurations::createAVD(QWidget *parent, int minApiLevel, QString targetArch) const
{
    QDialog d(parent);
    Ui::AddNewAVDDialog avdDialog;
    avdDialog.setupUi(&d);
    // NOTE: adb list targets does actually include information on which abis are supported per apilevel
    // we aren't using that information here
    avdDialog.targetComboBox->addItems(sdkTargets(minApiLevel));

    if (targetArch.isEmpty())
        avdDialog.abiComboBox->addItems(QStringList()
                                        << QLatin1String("armeabi-v7a")
                                        << QLatin1String("armeabi")
                                        << QLatin1String("x86")
                                        << QLatin1String("mips"));
    else
        avdDialog.abiComboBox->addItems(QStringList(targetArch));

    if (!avdDialog.targetComboBox->count()) {
        QMessageBox::critical(0, tr("Error Creating AVD"),
                              tr("Cannot create a new AVD. No sufficiently recent Android SDK available.\n"
                                 "Please install an SDK of at least API version %1.").
                              arg(minApiLevel));
        return QString();
    }

    QRegExp rx(QLatin1String("\\S+"));
    QRegExpValidator v(rx, 0);
    avdDialog.nameLineEdit->setValidator(&v);
    if (d.exec() != QDialog::Accepted)
        return QString();
    return createAVD(avdDialog.targetComboBox->currentText(), avdDialog.nameLineEdit->text(), avdDialog.abiComboBox->currentText(), avdDialog.sizeSpinBox->value());
}

QString AndroidConfigurations::createAVD(const QString &target, const QString &name, const QString &abi, int sdcardSize ) const
{
    QProcess proc;
    proc.setProcessEnvironment(androidToolEnvironment().toProcessEnvironment());
    proc.start(androidToolPath().toString(),
               QStringList() << QLatin1String("create") << QLatin1String("avd")
               << QLatin1String("-t") << target
               << QLatin1String("-n") << name
               << QLatin1String("-b") << abi
               << QLatin1String("-c") << QString::fromLatin1("%1M").arg(sdcardSize));
    if (!proc.waitForStarted())
        return QString();

    proc.write(QByteArray("yes\n")); // yes to "Do you wish to create a custom hardware profile"

    QByteArray question;
    while (true) {
        proc.waitForReadyRead(500);
        question += proc.readAllStandardOutput();
        if (question.endsWith(QByteArray("]:"))) {
            // truncate to last line
            int index = question.lastIndexOf(QByteArray("\n"));
            if (index != -1)
                question = question.mid(index);
            if (question.contains("hw.gpu.enabled"))
                proc.write(QByteArray("yes\n"));
            else
                proc.write(QByteArray("\n"));
            question.clear();
        }

        if (proc.state() != QProcess::Running)
            break;
    }

    Core::MessageManager::write(QString::fromLocal8Bit(question), Core::MessageManager::Flash);

    proc.waitForFinished();

    if (proc.exitCode()) // error!
        return QString();
    return name;
}

bool AndroidConfigurations::removeAVD(const QString &name) const
{
    QProcess proc;
    proc.setProcessEnvironment(androidToolEnvironment().toProcessEnvironment());
    proc.start(androidToolPath().toString(),
               QStringList() << QLatin1String("delete") << QLatin1String("avd")
               << QLatin1String("-n") << name);
    if (!proc.waitForFinished(5000)) {
        proc.terminate();
        return false;
    }
    return !proc.exitCode();
}

QVector<AndroidDeviceInfo> AndroidConfigurations::androidVirtualDevices() const
{
    QVector<AndroidDeviceInfo> devices;
    QProcess proc;
    proc.setProcessEnvironment(androidToolEnvironment().toProcessEnvironment());
    proc.start(androidToolPath().toString(),
               QStringList() << QLatin1String("list") << QLatin1String("avd")); // list available AVDs
    if (!proc.waitForFinished(5000)) {
        proc.terminate();
        return devices;
    }
    QList<QByteArray> avds = proc.readAll().trimmed().split('\n');
    if (avds.empty())
        return devices;

    while (avds.first().startsWith("* daemon"))
        avds.removeFirst(); // remove the daemon logs
    avds.removeFirst(); // remove "List of devices attached" header line

    AndroidDeviceInfo dev;
    for (int i = 0; i < avds.size(); i++) {
        QString line = QLatin1String(avds[i]);
        if (!line.contains(QLatin1String("Name:")))
            continue;

        dev.serialNumber = line.mid(line.indexOf(QLatin1Char(':')) + 2).trimmed();
        ++i;
        for (; i < avds.size(); ++i) {
            line = QLatin1String(avds[i]);
            if (line.contains(QLatin1String("---------")))
                break;
            if (line.contains(QLatin1String("Target:")))
                dev.sdk = line.mid(line.lastIndexOf(QLatin1Char(' '))).remove(QLatin1Char(')')).toInt();
            if (line.contains(QLatin1String("ABI:")))
                dev.cpuAbi = QStringList() << line.mid(line.lastIndexOf(QLatin1Char(' '))).trimmed();
        }
        // armeabi-v7a devices can also run armeabi code
        if (dev.cpuAbi == QStringList(QLatin1String("armeabi-v7a")))
            dev.cpuAbi << QLatin1String("armeabi");
        dev.unauthorized = false;
        dev.type = AndroidDeviceInfo::Emulator;
        devices.push_back(dev);
    }
    qSort(devices.begin(), devices.end(), androidDevicesLessThan);

    return devices;
}

QString AndroidConfigurations::startAVD(const QString &name, int apiLevel, QString cpuAbi) const
{
    if (!findAvd(apiLevel, cpuAbi).isEmpty() || startAVDAsync(name))
        return waitForAvd(apiLevel, cpuAbi);
    return QString();
}

bool AndroidConfigurations::startAVDAsync(const QString &avdName) const
{
    QProcess *avdProcess = new QProcess();
    connect(this, SIGNAL(destroyed()), avdProcess, SLOT(deleteLater()));
    connect(avdProcess, SIGNAL(finished(int)), avdProcess, SLOT(deleteLater()));

    // start the emulator
    avdProcess->start(emulatorToolPath().toString(),
                        QStringList() << QLatin1String("-partition-size") << QString::number(config().partitionSize)
                        << QLatin1String("-avd") << avdName);
    if (!avdProcess->waitForStarted(-1)) {
        delete avdProcess;
        return false;
    }
    return true;
}

QString AndroidConfigurations::findAvd(int apiLevel, const QString &cpuAbi) const
{
    QVector<AndroidDeviceInfo> devices = connectedDevices();
    foreach (AndroidDeviceInfo device, devices) {
        if (!device.serialNumber.startsWith(QLatin1String("emulator")))
            continue;
        if (!device.cpuAbi.contains(cpuAbi))
            continue;
        if (device.sdk != apiLevel)
            continue;
        return device.serialNumber;
    }
    return QString();
}

bool AndroidConfigurations::isConnected(const QString &serialNumber) const
{
    QVector<AndroidDeviceInfo> devices = connectedDevices();
    foreach (AndroidDeviceInfo device, devices) {
        if (device.serialNumber == serialNumber)
            return true;
    }
    return false;
}

bool AndroidConfigurations::waitForBooted(const QString &serialNumber, const QFutureInterface<bool> &fi) const
{
    // found a serial number, now wait until it's done booting...
    for (int i = 0; i < 60; ++i) {
        if (fi.isCanceled())
            return false;
        if (hasFinishedBooting(serialNumber)) {
            return true;
        } else {
            Utils::sleep(2000);
            if (!isConnected(serialNumber)) // device was disconnected
                return false;
        }
    }
    return false;
}

QString AndroidConfigurations::waitForAvd(int apiLevel, const QString &cpuAbi, const QFutureInterface<bool> &fi) const
{
    // we cannot use adb -e wait-for-device, since that doesn't work if a emulator is already running
    // 60 rounds of 2s sleeping, two minutes for the avd to start
    QString serialNumber;
    for (int i = 0; i < 60; ++i) {
        if (fi.isCanceled())
            return QString();
        serialNumber = findAvd(apiLevel, cpuAbi);
        if (!serialNumber.isEmpty())
            return waitForBooted(serialNumber, fi) ?  serialNumber : QString();
        Utils::sleep(2000);
    }
    return QString();
}

bool AndroidConfigurations::isBootToQt(const QString &device) const
{
    // workaround for '????????????' serial numbers
    QStringList arguments = AndroidDeviceInfo::adbSelector(device);
    arguments << QLatin1String("shell")
              << QLatin1String("ls -l /system/bin/appcontroller || ls -l /usr/bin/appcontroller && echo Boot2Qt");

    QProcess adbProc;
    adbProc.start(adbToolPath().toString(), arguments);
    if (!adbProc.waitForFinished(5000)) {
        adbProc.kill();
        return false;
    }
    return adbProc.readAll().contains("Boot2Qt");
}

int AndroidConfigurations::getSDKVersion(const QString &device) const
{
    // workaround for '????????????' serial numbers
    QStringList arguments = AndroidDeviceInfo::adbSelector(device);
    arguments << QLatin1String("shell") << QLatin1String("getprop")
              << QLatin1String("ro.build.version.sdk");

    QProcess adbProc;
    adbProc.start(adbToolPath().toString(), arguments);
    if (!adbProc.waitForFinished(5000)) {
        adbProc.kill();
        return -1;
    }
    return adbProc.readAll().trimmed().toInt();
}

//!
//! \brief AndroidConfigurations::getProductModel
//! \param device serial number
//! \return the produce model of the device or if that cannot be read the serial number
//!
QString AndroidConfigurations::getProductModel(const QString &device) const
{
    if (m_serialNumberToDeviceName.contains(device))
        return m_serialNumberToDeviceName.value(device);
    // workaround for '????????????' serial numbers
    QStringList arguments = AndroidDeviceInfo::adbSelector(device);
    arguments << QLatin1String("shell") << QLatin1String("getprop")
              << QLatin1String("ro.product.model");

    QProcess adbProc;
    adbProc.start(adbToolPath().toString(), arguments);
    if (!adbProc.waitForFinished(5000)) {
        adbProc.kill();
        return device;
    }
    QString model = QString::fromLocal8Bit(adbProc.readAll().trimmed());
    if (model.isEmpty())
        return device;
    if (!device.startsWith(QLatin1String("????")))
        m_serialNumberToDeviceName.insert(device, model);
    return model;
}

bool AndroidConfigurations::hasFinishedBooting(const QString &device) const
{
    QStringList arguments = AndroidDeviceInfo::adbSelector(device);
    arguments << QLatin1String("shell") << QLatin1String("getprop")
              << QLatin1String("init.svc.bootanim");

    QProcess adbProc;
    adbProc.start(adbToolPath().toString(), arguments);
    if (!adbProc.waitForFinished(5000)) {
        adbProc.kill();
        return false;
    }
    QString value = QString::fromLocal8Bit(adbProc.readAll().trimmed());
    if (value == QLatin1String("stopped"))
        return true;
    return false;
}

QStringList AndroidConfigurations::getAbis(const QString &device) const
{
    QStringList result;
    for (int i = 1; i < 6; ++i) {
        QStringList arguments = AndroidDeviceInfo::adbSelector(device);
        arguments << QLatin1String("shell") << QLatin1String("getprop");
        if (i == 1)
            arguments << QLatin1String("ro.product.cpu.abi");
        else
            arguments << QString::fromLatin1("ro.product.cpu.abi%1").arg(i);

        QProcess adbProc;
        adbProc.start(adbToolPath().toString(), arguments);
        if (!adbProc.waitForFinished(5000)) {
            adbProc.kill();
            return result;
        }
        QString abi = QString::fromLocal8Bit(adbProc.readAll().trimmed());
        if (abi.isEmpty())
            break;
        result << abi;
    }
    return result;
}

QString AndroidConfigurations::highestAndroidSdk() const
{
    if (m_availableSdkPlatforms.isEmpty())
        return QString();
    return QLatin1String("android-") + QString::number(m_availableSdkPlatforms.first());
}

QString AndroidConfigurations::bestNdkPlatformMatch(const QString &targetAPI) const
{
    int target = targetAPI.mid(targetAPI.lastIndexOf(QLatin1Char('-')) + 1).toInt();
    foreach (int apiLevel, m_availableNdkPlatforms) {
        if (apiLevel <= target)
            return QString::fromLatin1("android-%1").arg(apiLevel);
    }
    return QLatin1String("android-8");
}

QStringList AndroidConfigurations::makeExtraSearchDirectories() const
{
    return m_config.makeExtraSearchDirectories;
}

static bool equalKits(Kit *a, Kit *b)
{
    return ToolChainKitInformation::toolChain(a) == ToolChainKitInformation::toolChain(b)
            && QtSupport::QtKitInformation::qtVersion(a) == QtSupport::QtKitInformation::qtVersion(b);
}

void AndroidConfigurations::updateAutomaticKitList()
{
    QList<AndroidToolChain *> toolchains;
    if (AndroidConfigurations::instance().config().automaticKitCreation) {
        // having a empty toolchains list will remove all autodetected kits for android
        // exactly what we want in that case
        foreach (ToolChain *tc, ToolChainManager::toolChains()) {
            if (!tc->isAutoDetected())
                continue;
            if (tc->type() != QLatin1String(Constants::ANDROID_TOOLCHAIN_TYPE))
                continue;
            toolchains << static_cast<AndroidToolChain *>(tc);
        }
    }

    QList<Kit *> existingKits;

    foreach (Kit *k, KitManager::kits()) {
        if (DeviceKitInformation::deviceId(k) != Core::Id(Constants::ANDROID_DEVICE_ID))
            continue;
        if (!k->isAutoDetected())
            continue;
        if (k->isSdkProvided())
            continue;

        // Update code for 3.0 beta, which shipped with a bug for the debugger settings
        ProjectExplorer::ToolChain *tc =ToolChainKitInformation::toolChain(k);
        if (tc && Debugger::DebuggerKitInformation::debuggerCommand(k) != tc->suggestedDebugger()) {
            Debugger::DebuggerItem debugger;
            debugger.setCommand(tc->suggestedDebugger());
            debugger.setEngineType(Debugger::GdbEngineType);
            debugger.setDisplayName(tr("Android Debugger for %1").arg(tc->displayName()));
            debugger.setAutoDetected(true);
            debugger.setAbi(tc->targetAbi());
            QVariant id = Debugger::DebuggerItemManager::registerDebugger(debugger);
            Debugger::DebuggerKitInformation::setDebugger(k, id);
        }
        existingKits << k;
    }

    QMap<Abi::Architecture, QList<QtSupport::BaseQtVersion *> > qtVersionsForArch;
    foreach (QtSupport::BaseQtVersion *qtVersion, QtSupport::QtVersionManager::versions()) {
        if (qtVersion->type() != QLatin1String(Constants::ANDROIDQT))
            continue;
        QList<Abi> qtAbis = qtVersion->qtAbis();
        if (qtAbis.empty())
            continue;
        qtVersionsForArch[qtAbis.first().architecture()].append(qtVersion);
    }

    DeviceManager *dm = DeviceManager::instance();
    IDevice::ConstPtr device = dm->find(Core::Id(Constants::ANDROID_DEVICE_ID));
    if (device.isNull()) {
        // no device, means no sdk path
        foreach (Kit *k, existingKits)
            KitManager::deregisterKit(k);
        return;
    }

    // register new kits
    QList<Kit *> newKits;
    foreach (AndroidToolChain *tc, toolchains) {
        if (tc->isSecondaryToolChain())
            continue;
        QList<QtSupport::BaseQtVersion *> qtVersions = qtVersionsForArch.value(tc->targetAbi().architecture());
        foreach (QtSupport::BaseQtVersion *qt, qtVersions) {
            Kit *newKit = new Kit;
            newKit->setAutoDetected(true);
            newKit->setIconPath(Utils::FileName::fromString(QLatin1String(Constants::ANDROID_SETTINGS_CATEGORY_ICON)));
            DeviceTypeKitInformation::setDeviceTypeId(newKit, Core::Id(Constants::ANDROID_DEVICE_TYPE));
            ToolChainKitInformation::setToolChain(newKit, tc);
            QtSupport::QtKitInformation::setQtVersion(newKit, qt);
            DeviceKitInformation::setDevice(newKit, device);

            Debugger::DebuggerItem debugger;
            debugger.setCommand(tc->suggestedDebugger());
            debugger.setEngineType(Debugger::GdbEngineType);
            debugger.setDisplayName(tr("Android Debugger for %1").arg(tc->displayName()));
            debugger.setAutoDetected(true);
            debugger.setAbi(tc->targetAbi());
            QVariant id = Debugger::DebuggerItemManager::registerDebugger(debugger);
            Debugger::DebuggerKitInformation::setDebugger(newKit, id);

            AndroidGdbServerKitInformation::setGdbSever(newKit, tc->suggestedGdbServer());
            newKit->makeSticky();
            newKits << newKit;
        }
    }

    for (int i = existingKits.count() - 1; i >= 0; --i) {
        Kit *existingKit = existingKits.at(i);
        for (int j = 0; j < newKits.count(); ++j) {
            Kit *newKit = newKits.at(j);
            if (equalKits(existingKit, newKit)) {
                // Kit is already registered, nothing to do
                newKits.removeAt(j);
                existingKits.at(i)->makeSticky();
                existingKits.removeAt(i);
                KitManager::deleteKit(newKit);
                j = newKits.count();
            }
        }
    }

    foreach (Kit *k, existingKits) {
        ProjectExplorer::ToolChain *tc = ToolChainKitInformation::toolChain(k);
        QtSupport::BaseQtVersion *qtVersion = QtSupport::QtKitInformation::qtVersion(k);
        if (tc && tc->type() == QLatin1String(Constants::ANDROID_TOOLCHAIN_TYPE)
                && qtVersion && qtVersion->type() == QLatin1String(Constants::ANDROIDQT)) {
            k->makeUnSticky();
            k->setAutoDetected(false);
        } else {
            KitManager::deregisterKit(k);
        }
    }

    foreach (Kit *kit, newKits) {
        AndroidToolChain *tc = static_cast<AndroidToolChain *>(ToolChainKitInformation::toolChain(kit));
        AndroidQtVersion *qt = static_cast<AndroidQtVersion *>(QtSupport::QtKitInformation::qtVersion(kit));
        kit->setDisplayName(tr("Android for %1 (GCC %2, Qt %3)")
                            .arg(qt->targetArch())
                            .arg(tc->ndkToolChainVersion())
                            .arg(qt->qtVersionString()));
        KitManager::registerKit(kit);
    }
}

/**
 * Workaround for '????????????' serial numbers
 * @return ("-d") for buggy devices, ("-s", <serial no>) for normal
 */
QStringList AndroidDeviceInfo::adbSelector(const QString &serialNumber)
{
    if (serialNumber.startsWith(QLatin1String("????")))
        return QStringList() << QLatin1String("-d");
    return QStringList() << QLatin1String("-s") << serialNumber;
}

AndroidConfigurations &AndroidConfigurations::instance(QObject *parent)
{
    if (m_instance == 0)
        m_instance = new AndroidConfigurations(parent);
    return *m_instance;
}

void AndroidConfigurations::save()
{
    QSettings *settings = Core::ICore::settings();
    settings->beginGroup(SettingsGroup);
    m_config.save(*settings);
    settings->endGroup();
}

AndroidConfigurations::AndroidConfigurations(QObject *parent)
    : QObject(parent)
{
    load();
    updateAvailableNdkPlatforms();
    updateAvailableSdkPlatforms();

    connect(ProjectExplorer::SessionManager::instance(), SIGNAL(projectRemoved(ProjectExplorer::Project*)),
            this, SLOT(clearDefaultDevices(ProjectExplorer::Project*)));
}

Utils::FileName javaHomeForJavac(const QString &location)
{
    QFileInfo fileInfo(location);
    int tries = 5;
    while (tries > 0) {
        QDir dir = fileInfo.dir();
        dir.cdUp();
        if (QFileInfo(dir.filePath(QLatin1String("lib/tools.jar"))).exists())
            return Utils::FileName::fromString(dir.path());
        if (fileInfo.isSymLink())
            fileInfo.setFile(fileInfo.symLinkTarget());
        else
            break;
        --tries;
    }
    return Utils::FileName();
}

void AndroidConfigurations::load()
{
    bool saveSettings = false;
    QSettings *settings = Core::ICore::settings();
    settings->beginGroup(SettingsGroup);
    m_config = AndroidConfig(*settings);

    if (m_config.antLocation.isEmpty()) {
        Environment env = Environment::systemEnvironment();
        QString location = env.searchInPath(QLatin1String("ant"));
        QFileInfo fi(location);
        if (fi.exists() && fi.isExecutable() && !fi.isDir()) {
            m_config.antLocation = FileName::fromString(location);
            saveSettings = true;
        }
    }

    if (m_config.openJDKLocation.isEmpty()) {
        if (HostOsInfo::isLinuxHost()) {
            Environment env = Environment::systemEnvironment();
            QString location = env.searchInPath(QLatin1String("javac"));
            QFileInfo fi(location);
            if (fi.exists() && fi.isExecutable() && !fi.isDir()) {
                m_config.openJDKLocation = javaHomeForJavac(location);
                saveSettings = true;
            }
        } else if (HostOsInfo::isMacHost()) {
            QString javaHome = QLatin1String("/System/Library/Frameworks/JavaVM.framework/Versions/CurrentJDK/Home");
            if (QFileInfo(javaHome).exists())
                m_config.openJDKLocation = Utils::FileName::fromString(javaHome);
        } else if (HostOsInfo::isWindowsHost()) {
            QSettings settings(QLatin1String("HKEY_LOCAL_MACHINE\\SOFTWARE\\Javasoft\\Java Development Kit"), QSettings::NativeFormat);
            QStringList allVersions = settings.childGroups();
            QString javaHome;
            int major = -1;
            int minor = -1;
            foreach (const QString &version, allVersions) {
                QStringList parts = version.split(QLatin1String("."));
                if (parts.size() != 2) // not interested in 1.7.0_u21
                    continue;
                bool okMajor, okMinor;
                int tmpMajor = parts.at(0).toInt(&okMajor);
                int tmpMinor = parts.at(1).toInt(&okMinor);
                if (!okMajor || !okMinor)
                    continue;
                if (tmpMajor > major
                        || (tmpMajor == major
                            && tmpMinor > minor)) {
                    settings.beginGroup(version);
                    QString tmpJavaHome = settings.value(QLatin1String("JavaHome")).toString();
                    settings.endGroup();
                    if (!QFileInfo(tmpJavaHome).exists())
                        continue;

                    major = tmpMajor;
                    minor = tmpMinor;
                    javaHome = tmpJavaHome;
                }
            }
            if (!javaHome.isEmpty()) {
                m_config.openJDKLocation = FileName::fromString(javaHome);
                saveSettings = true;
            }
        }
    }

    settings->endGroup();

    if (saveSettings)
        save();
}

void AndroidConfigurations::updateAndroidDevice()
{
    DeviceManager * const devMgr = DeviceManager::instance();
    if (adbToolPath().toFileInfo().exists())
        devMgr->addDevice(IDevice::Ptr(new Internal::AndroidDevice));
    else if (devMgr->find(Constants::ANDROID_DEVICE_ID))
        devMgr->removeDevice(Core::Id(Constants::ANDROID_DEVICE_ID));
}

AndroidConfigurations *AndroidConfigurations::m_instance = 0;

} // namespace Internal
} // namespace Android

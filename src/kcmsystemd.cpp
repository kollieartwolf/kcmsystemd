/*******************************************************************************
 * Copyright (C) 2013 Ragnar Thomsen <rthomsen6@gmail.com>                     *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify it     *
 * under the terms of the GNU General Public License as published by the Free  *
 * Software Foundation, either version 3 of the License, or (at your option)   *
 * any later version.                                                          *
 *                                                                             *
 * This program is distributed in the hope that it will be useful, but WITHOUT *
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for    *
 * more details.                                                               *
 *                                                                             *
 * You should have received a copy of the GNU General Public License along     *
 * with this program. If not, see <http://www.gnu.org/licenses/>.              *
 *******************************************************************************/

#include "kcmsystemd.h"
#include "reslimits.h"
#include "environ.h"
#include <config.h>

#include <QTextStream>
#include <QDir>

#include <KAboutData>
#include <KPluginFactory>
#include <KMessageBox>
#include <KAuth/ActionWatcher>
using namespace KAuth;

#include <boost/filesystem.hpp>

K_PLUGIN_FACTORY(kcmsystemdFactory, registerPlugin<kcmsystemd>();)
K_EXPORT_PLUGIN(kcmsystemdFactory("kcmsystemd"))

// Declare static member variables
QVariantMap kcmsystemd::resLimits;
QList<QPair<QString, QString> > kcmsystemd::environ;
bool kcmsystemd::resLimitsChanged = 0;
bool kcmsystemd::environChanged = 0;

// Declare global variables
float perDiskUsageValue, perDiskFreeValue, perSizeFilesValue, volDiskUsageValue, volDiskFreeValue, volSizeFilesValue = 0;
int timesLoad = 0;
QString etcDir = "";

kcmsystemd::kcmsystemd(QWidget *parent, const QVariantList &list) : KCModule(kcmsystemdFactory::componentData(), parent, list)
{
  KAboutData *about = new KAboutData("kcmsystemd", "kcmsystemd", ki18nc("@title", "KDE Systemd Control Module"), KCM_SYSTEMD_VERSION, ki18nc("@title", "A KDE Control Module for configuring Systemd."), KAboutData::License_GPL_V3, ki18nc("@info:credit", "Copyright (C) 2013 Ragnar Thomsen"), KLocalizedString(), "https://github.com/rthomsen/kcmsystemd");
  about->addAuthor(ki18nc("@info:credit", "Ragnar Thomsen"), ki18nc("@info:credit", "Main Developer"), "rthomsen6@gmail.com");
  setAboutData(about);
  
  ui.setupUi(this);
  setNeedsAuthorization(true);
  
  // Use pkg-config to see if systemd is installed
  QProcess pkgConfig (this);
  pkgConfig.start("pkg-config", QStringList() << "--exists" << "libsystemd-daemon");
  pkgConfig.waitForFinished(5000);
  if (pkgConfig.exitCode() != 0)
    ui.stackedWidget->setCurrentIndex(1);
  
  // Use pkg-config to get systemd version
  pkgConfigVer = new QProcess(this);
  connect(pkgConfigVer, SIGNAL(readyReadStandardOutput()), this, SLOT(slotVersion()));
  pkgConfigVer->start("pkg-config", QStringList() << "--modversion" << "libsystemd-daemon");

  // Find the configuration directory
  if (QDir("/etc/systemd").exists()) {
    etcDir = "/etc/systemd";
  } else if (QDir("/usr/etc/systemd").exists()) {
    etcDir = "/usr/etc/systemd";
  } else {
    // Failed to find systemd config directory
    ui.stackedWidget->setCurrentIndex(1);    
    ui.lblFailMessage->setText(i18n("Unable to find directory with systemd configuration files."));
    return;
  }
  
  // Use boost to find persistent partition size
  boost::filesystem::path pp ("/var/log");
  boost::filesystem::space_info logPpart = boost::filesystem::space(pp);
  partPersSizeMB = logPpart.capacity / 1024 / 1024;
  
  // Use boost to find volatile partition size
  boost::filesystem::path pv ("/run/log");
  boost::filesystem::space_info logVpart = boost::filesystem::space(pv);
  partVolaSizeMB = logVpart.capacity / 1024 / 1024;
  
  /* Check if systemd binaries exist
  QFile systemd1 ("/bin/systemd");
  QFile systemd2 ("/usr/bin/systemd");
  if (!systemd1.exists() && !systemd2.exists())
    ui.stackedWidget->setCurrentIndex(1);
  else
    ui.stackedWidget->setCurrentIndex(0);
  */
  
  // Connect signals for system.conf
  connect(ui.cmbLogLevel, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbLogTarget, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.chkLogColor, SIGNAL(stateChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.chkLogLocation, SIGNAL(stateChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.chkDumpCore, SIGNAL(stateChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.chkCrashShell, SIGNAL(stateChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.chkShowStatus, SIGNAL(stateChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbCrashVT, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.leCPUAffinity, SIGNAL(textEdited(QString)), this, SLOT(slotDefaultChanged()));
  connect(ui.chkCPUAffinity, SIGNAL(stateChanged(int)), this, SLOT(slotCPUAffinityChanged()));
  connect(ui.leDefControllers, SIGNAL(textEdited(QString)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbDefStdOutput, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbDefStdError, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.leJoinControllers, SIGNAL(textEdited(QString)), this, SLOT(slotDefaultChanged()));
  connect(ui.spnRuntimeWatchdog, SIGNAL(valueChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbRuntimeWatchdog, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.spnShutdownWatchdog, SIGNAL(valueChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbShutdownWatchdog, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.leCapBoundSet, SIGNAL(textEdited(QString)), this, SLOT(slotDefaultChanged()));
  connect(ui.spnTimerSlack, SIGNAL(valueChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbTimerSlack, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.btnResourceLimits, SIGNAL(clicked()), this, SLOT(slotOpenResourceLimits()));
  connect(ui.btnEnviron, SIGNAL(clicked()), this, SLOT(slotOpenEnviron()));
  
  // Connect signals for journald.conf:
  connect(ui.chkCompressLogs, SIGNAL(stateChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.chkFwdSecureSealing, SIGNAL(stateChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbStorage, SIGNAL(currentIndexChanged(int)), this, SLOT(slotStorageChanged()));
  connect(ui.spnSync, SIGNAL(valueChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbSync, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.radLogin, SIGNAL(toggled(bool)), this, SLOT(slotDefaultChanged()));
  connect(ui.radUID, SIGNAL(toggled(bool)), this, SLOT(slotDefaultChanged()));
  connect(ui.radNone, SIGNAL(toggled(bool)), this, SLOT(slotDefaultChanged()));
  connect(ui.spnDiskUsage, SIGNAL(valueChanged(int)), this, SLOT(slotSpnDiskUsageChanged()));
  connect(ui.spnDiskFree, SIGNAL(valueChanged(int)), this, SLOT(slotSpnDiskFreeChanged()));
  connect(ui.spnSizeFiles, SIGNAL(valueChanged(int)), this, SLOT(slotSpnSizeFilesChanged()));
  connect(ui.chkMaxRetention, SIGNAL(stateChanged(int)), this, SLOT(slotMaxRetentionChanged()));
  connect(ui.spnMaxRetention, SIGNAL(valueChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbMaxRetention, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.chkMaxFile, SIGNAL(stateChanged(int)), this, SLOT(slotMaxFileChanged()));
  connect(ui.spnMaxFile, SIGNAL(valueChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbMaxFile, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.chkFwdSyslog, SIGNAL(stateChanged(int)), this, SLOT(slotFwdToSyslogChanged()));
  connect(ui.chkFwdKmsg, SIGNAL(stateChanged(int)), this, SLOT(slotFwdToKmsgChanged()));
  connect(ui.chkFwdConsole, SIGNAL(stateChanged(int)), this, SLOT(slotFwdToConsoleChanged()));
  connect(ui.leTTYPath, SIGNAL(textEdited(QString)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbLevelStore, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbLevelKmsg, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbLevelSyslog, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbLevelConsole, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  
  // Connect signals for logind.conf
  connect(ui.spnAutoVTs, SIGNAL(valueChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.spnReserveVT, SIGNAL(valueChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.chkKillUserProc, SIGNAL(stateChanged(int)), this, SLOT(slotKillUserProcessesChanged()));
  connect(ui.leKillOnlyUsers, SIGNAL(textEdited(QString)), this, SLOT(slotDefaultChanged()));
  connect(ui.leKillExcludeUsers, SIGNAL(textEdited(QString)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbIdleAction, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.spnIdleActionSec, SIGNAL(valueChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbIdleActionSec, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbPowerKey, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbSuspendKey, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbHibernateKey, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.cmbLidSwitch, SIGNAL(currentIndexChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.chkPowerKey, SIGNAL(stateChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.chkSuspendKey, SIGNAL(stateChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.chkHibernateKey, SIGNAL(stateChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.chkLidSwitch, SIGNAL(stateChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.spnInhibDelayMax, SIGNAL(valueChanged(int)), this, SLOT(slotDefaultChanged()));
  connect(ui.leControllers, SIGNAL(textEdited(QString)), this, SLOT(slotDefaultChanged()));
  connect(ui.leResetControllers, SIGNAL(textEdited(QString)), this, SLOT(slotDefaultChanged()));
  
  // layout()->setSizeConstraint(QLayout::SetFixedSize);
}

void kcmsystemd::load()
{
  // Find number of CPUs:
  // unsigned short numCPU = sysconf( _SC_NPROCESSORS_ONLN );
  
  // Only initialize the interface once
  if (timesLoad == 0)
    initializeInterface();
  
  // Read the three configuration files
  readSystemConf();
  readJournaldConf();
  readLogindConf();
}

void kcmsystemd::initializeInterface()
{
  timesLoad = timesLoad + 1;

  isPersistent = 0;
 
  // Initialize the default disk usage limits and spinboxes
  perDiskUsageValue = 0.1 * partPersSizeMB;
  perDiskFreeValue = 0.15 * partPersSizeMB;
  perSizeFilesValue = 0.0125 * partPersSizeMB;
  volDiskUsageValue = 0.1 * partVolaSizeMB;
  volDiskFreeValue = 0.15 * partVolaSizeMB;
  volSizeFilesValue = 0.0125 * partVolaSizeMB;
  if (isPersistent) {
    ui.spnDiskUsage->setValue(perDiskUsageValue + 0.5);
    ui.spnDiskFree->setValue(perDiskFreeValue + 0.5);
    ui.spnSizeFiles->setValue(perSizeFilesValue + 0.5);
  } else {
    ui.spnDiskUsage->setValue(volDiskUsageValue + 0.5);
    ui.spnDiskFree->setValue(volDiskFreeValue + 0.5);
    ui.spnSizeFiles->setValue(volSizeFilesValue + 0.5);
  }
    
  // Set allowed values for comboboxes:
  QStringList allowLogLevel = QStringList() << "emerg" << "alert" << "crit" << "err" << "warning" << "notice" << "info" << "debug";
  QStringList allowLogTarget = QStringList() << "console" << "journal" << "syslog" << "kmsg" << "journal-or-kmsg" << "syslog-or-kmsg" << "null";
  QStringList allowStdIO = QStringList() << "inherit" << "null" << "tty" << "journal" << "journal+console" << "syslog" << "syslog+console" << "kmsg" << "kmsg+console";
  QStringList allowTimeUnits = QStringList() << "seconds" << "minutes" << "hours" << "days" << "weeks";
  QStringList allowTimeUnitsRateInterval = QStringList() << "microseconds" << "milliseconds" << "seconds" << "minutes" << "hours";  
  QStringList allowStorage = QStringList() << "volatile" << "persistent" << "auto" << "none";
  QStringList allowSplitMode = QStringList() << "login" << "UID" << "none";
  QStringList allowDiskUsage = QStringList() << "bytes" << "kilobytes" << "megabytes" << "gigabytes" << "terabytes" << "petabytes" << "exabytes";
  QStringList allowPowerEvents = QStringList() << "ignore" << "poweroff" << "reboot" << "halt" << "kexec" << "suspend" << "hibernate" << "hybrid-sleep" << "lock";
  
  // Populate comboboxes:
  for (int i = 0; i < allowLogLevel.size(); i++)
  {
    ui.cmbLogLevel->addItem(allowLogLevel[i]);
    ui.cmbLevelStore->addItem(allowLogLevel[i]);
    ui.cmbLevelSyslog->addItem(allowLogLevel[i]);
    ui.cmbLevelKmsg->addItem(allowLogLevel[i]);
    ui.cmbLevelConsole->addItem(allowLogLevel[i]);
  }
  ui.cmbLogLevel->setCurrentIndex(ui.cmbLogLevel->findText("info"));
  for (int i = 0; i < allowLogTarget.size(); i++)
  {
    ui.cmbLogTarget->addItem(allowLogTarget[i]);
  }
  ui.cmbLogTarget->setCurrentIndex(ui.cmbLogTarget->findText("journal"));
  ui.cmbCrashVT->addItem("Off");
  for (int i = 1; i < 9; i++)
  {
    ui.cmbCrashVT->addItem(QString::number(i));
  }
  for (int i = 0; i < allowStdIO.size(); i++)
  {
    ui.cmbDefStdOutput->addItem(allowStdIO[i]);
    ui.cmbDefStdError->addItem(allowStdIO[i]);
  }
  ui.cmbDefStdOutput->setCurrentIndex(3);
  ui.cmbDefStdError->setCurrentIndex(0);
  ui.cmbRuntimeWatchdog->addItem("milliseconds");
  ui.cmbShutdownWatchdog->addItem("milliseconds");
  ui.cmbTimerSlack->addItem("nanoseconds");
  ui.cmbTimerSlack->addItem("milliseconds");
  for (int i = 0; i < allowTimeUnits.size(); i++)
  {
    ui.cmbRuntimeWatchdog->addItem(allowTimeUnits[i]);
    ui.cmbShutdownWatchdog->addItem(allowTimeUnits[i]);
    ui.cmbTimerSlack->addItem(allowTimeUnits[i]);
    ui.cmbSync->addItem(allowTimeUnits[i]);
    ui.cmbMaxRetention->addItem(allowTimeUnits[i]);
    ui.cmbMaxFile->addItem(allowTimeUnits[i]);
    ui.cmbIdleActionSec->addItem(allowTimeUnits[i]);
  }
  ui.cmbRuntimeWatchdog->setCurrentIndex(ui.cmbRuntimeWatchdog->findText("seconds"));
  ui.cmbShutdownWatchdog->setCurrentIndex(ui.cmbShutdownWatchdog->findText("minutes"));
  ui.cmbMaxRetention->addItem("months");
  ui.cmbMaxRetention->addItem("years");
  ui.cmbMaxFile->addItem("months");
  ui.cmbMaxFile->addItem("years");
  for (int i = 0; i < allowTimeUnitsRateInterval.size(); i++)
  {
    ui.cmbRateInterval->addItem(allowTimeUnitsRateInterval[i]); 
  }
  for (int i = 0; i < allowStorage.size(); i++)
  {
    ui.cmbStorage->addItem(allowStorage[i]);
  }
  ui.cmbStorage->setCurrentIndex(2);
  for (int i = 0; i < allowPowerEvents.size(); i++)
  {
    ui.cmbIdleAction->addItem(allowPowerEvents[i]);
    ui.cmbPowerKey->addItem(allowPowerEvents[i]);
    ui.cmbSuspendKey->addItem(allowPowerEvents[i]);
    ui.cmbHibernateKey->addItem(allowPowerEvents[i]);
    ui.cmbLidSwitch->addItem(allowPowerEvents[i]);
  }
  ui.cmbIdleAction->setCurrentIndex(ui.cmbIdleAction->findText("ignore"));
  ui.cmbPowerKey->setCurrentIndex(ui.cmbPowerKey->findText("poweroff"));
  ui.cmbSuspendKey->setCurrentIndex(ui.cmbSuspendKey->findText("suspend"));
  ui.cmbHibernateKey->setCurrentIndex(ui.cmbHibernateKey->findText("hibernate"));
  ui.cmbLidSwitch->setCurrentIndex(ui.cmbLidSwitch->findText("suspend"));
  ui.cmbIdleActionSec->removeItem(ui.cmbIdleActionSec->findText("days"));
  ui.cmbIdleActionSec->removeItem(ui.cmbIdleActionSec->findText("weeks"));
}

void kcmsystemd::readSystemConf()
{ 
  // Set keywords for parsing system.conf:
  QStringList systemids = QStringList() << "LogLevel" << "LogTarget" << "LogColor" 
    << "LogLocation" << "DumpCore" << "CrashShell" << "ShowStatus" << "CrashChVT" 
    << "CPUAffinity" << "DefaultControllers" << "DefaultStandardOutput"
    << "DefaultStandardError" << "JoinControllers" << "RuntimeWatchdogSec"
    << "ShutdownWatchdogSec" << "CapabilityBoundingSet" << "TimerSlackNSec" 
    << "DefaultLimitCPU" << "DefaultLimitFSIZE" << "DefaultLimitDATA"
    << "DefaultLimitSTACK" << "DefaultLimitCORE" << "DefaultLimitRSS"
    << "DefaultLimitNOFILE" << "DefaultLimitAS" << "DefaultLimitNPROC"
    << "DefaultLimitMEMLOCK" << "DefaultLimitLOCKS" << "DefaultLimitSIGPENDING"
    << "DefaultLimitMSGQUEUE" << "DefaultLimitNICE" << "DefaultLimitRTPRIO"
    << "DefaultLimitRTTIME" << "DefaultEnvironment";

  QFile systemfile (etcDir + "/system.conf");

  if (systemfile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&systemfile);
    QString line = in.readLine();

    while(!line.isNull()) {
    
      if (line.contains(systemids[0])) {
	if (line.trimmed().left(1) != "#" && !line.section("=",-1).trimmed().isEmpty())
	  ui.cmbLogLevel->setCurrentIndex(ui.cmbLogLevel->findText(line.section("=",-1).trimmed().toLower()));
      
      } else if (line.contains(systemids[1])) {
	if (line.trimmed().left(1) != "#" && !line.section("=",-1).trimmed().isEmpty())
	  ui.cmbLogTarget->setCurrentIndex(ui.cmbLogTarget->findText(line.section("=",-1).trimmed().toLower()));
      
      } else if (line.contains(systemids[2])) {
      if (line.trimmed().left(1) != "#" && !ToBoolDefOn(line.section('=',-1)))
	  ui.chkLogColor->setChecked(0);
      
      } else if (line.contains(systemids[3])) {
	if (line.trimmed().left(1) != "#" && !ToBoolDefOn(line.section('=',-1)))
	  ui.chkLogLocation->setChecked(0);
      
      } else if (line.contains(systemids[4])) {
	if (line.trimmed().left(1) != "#" && !ToBoolDefOn(line.section('=',-1)))
	  ui.chkDumpCore->setChecked(0);
      
      } else if (line.contains(systemids[5])) {
	if (line.trimmed().left(1) != "#" && ToBoolDefOff(line.section('=',-1)))
	  ui.chkCrashShell->setChecked(1);
	
      } else if (line.contains(systemids[6])) {
	if (line.trimmed().left(1) != "#" && !ToBoolDefOn(line.section('=',-1)))
	  ui.chkShowStatus->setChecked(0);
	
      } else if (line.contains(systemids[7])) {
	ui.cmbCrashVT->setCurrentIndex(0);
	if (line.trimmed().left(1) != "#" && !line.section("=",-1).trimmed().isEmpty())
	  if (line.section("=",-1).trimmed().toInt() == -1)
	    ui.cmbCrashVT->setCurrentIndex(ui.cmbCrashVT->findText("Off"));
	  else
	    ui.cmbCrashVT->setCurrentIndex(ui.cmbCrashVT->findText(line.section("=",-1).trimmed()));
	  
      } else if (line.contains(systemids[8])) {
	  ui.leCPUAffinity->setText(line.section('=',-1).trimmed());
	  if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty())
	    ui.chkCPUAffinity->setChecked(1);
	  else
	    ui.leCPUAffinity->setEnabled(0);

      } else if (line.contains(systemids[9])) {
	  if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty())
	    ui.leDefControllers->setText(line.section('=',-1).trimmed());
	  else
	    ui.leDefControllers->setText("cpu");
	  
      } else if (line.contains(systemids[10])) {
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty())
	  ui.cmbDefStdOutput->setCurrentIndex(ui.cmbDefStdOutput->findText(line.section("=",-1).trimmed().toLower()));
	
      } else if (line.contains(systemids[11])) {
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty())
	  ui.cmbDefStdError->setCurrentIndex(ui.cmbDefStdError->findText(line.section("=",-1).trimmed().toLower()));
	
      } else if (line.contains(systemids[12])) {
	  if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty())
	    ui.leJoinControllers->setText(line.section('=',-1).trimmed());
	  else
	    ui.leJoinControllers->setText("cpu,cpuacct");
	  
      } else if (line.contains(systemids[13])) {
	  if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	    QRegExp nmbrs("[0-9]+");
	    QRegExp timeunit("ms|min|h|d|w");
	    ui.spnRuntimeWatchdog->setValue(line.section("=",-1).trimmed().section(timeunit,0,0).toInt());
	    if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "ms") {
	      ui.cmbRuntimeWatchdog->setCurrentIndex(ui.cmbRuntimeWatchdog->findText("milliseconds"));
	    } else if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "min") {
	      ui.cmbRuntimeWatchdog->setCurrentIndex(ui.cmbRuntimeWatchdog->findText("minutes"));
	    } else if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "h") {
	      ui.cmbRuntimeWatchdog->setCurrentIndex(ui.cmbRuntimeWatchdog->findText("hours"));
	    } else if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "d") {
	      ui.cmbRuntimeWatchdog->setCurrentIndex(ui.cmbRuntimeWatchdog->findText("days"));
	    } else if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "w") {
	      ui.cmbRuntimeWatchdog->setCurrentIndex(ui.cmbRuntimeWatchdog->findText("weeks"));
	    } else {
	      ui.cmbRuntimeWatchdog->setCurrentIndex(ui.cmbRuntimeWatchdog->findText("seconds"));
	    }
	  }
      
      } else if (line.contains(systemids[14])) {
	  if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	    QRegExp nmbrs("[0-9]+");
	    QRegExp timeunit("ms|min|h|d|w");
	    ui.spnShutdownWatchdog->setValue(line.section("=",-1).trimmed().section(timeunit,0,0).toInt());
	    if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "ms")
	      ui.cmbShutdownWatchdog->setCurrentIndex(ui.cmbShutdownWatchdog->findText("milliseconds"));
	    else if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "min")
	      ui.cmbShutdownWatchdog->setCurrentIndex(ui.cmbShutdownWatchdog->findText("minutes"));
	    else if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "h")
	      ui.cmbShutdownWatchdog->setCurrentIndex(ui.cmbShutdownWatchdog->findText("hours"));
	    else if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "d")
	      ui.cmbShutdownWatchdog->setCurrentIndex(ui.cmbShutdownWatchdog->findText("days"));
	    else if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "w")
	      ui.cmbShutdownWatchdog->setCurrentIndex(ui.cmbShutdownWatchdog->findText("weeks"));
	    else
	      ui.cmbShutdownWatchdog->setCurrentIndex(ui.cmbShutdownWatchdog->findText("seconds"));
	  }
	
      } else if (line.contains(systemids[15])) {
	  if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty())
	    ui.leCapBoundSet->setText(line.section('=',-1).trimmed());
	  
      } else if (line.contains(systemids[16])) {
	  ui.cmbTimerSlack->setCurrentIndex(ui.cmbTimerSlack->findText("nanoseconds"));
	  if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	    QRegExp nmbrs("[0-9]+");
	    QRegExp timeunit("ms|s|min|h|d|w");
	    ui.spnTimerSlack->setValue(line.section("=",-1).trimmed().section(timeunit,0,0).toInt());
	    if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "ms")
	      ui.cmbTimerSlack->setCurrentIndex(ui.cmbTimerSlack->findText("milliseconds"));
	    else if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "s")
	      ui.cmbTimerSlack->setCurrentIndex(ui.cmbTimerSlack->findText("seconds"));
	    else if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "min")
	      ui.cmbTimerSlack->setCurrentIndex(ui.cmbTimerSlack->findText("minutes"));
	    else if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "h")
	      ui.cmbTimerSlack->setCurrentIndex(ui.cmbTimerSlack->findText("hours"));
	    else if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "d")
	      ui.cmbTimerSlack->setCurrentIndex(ui.cmbTimerSlack->findText("days"));
	    else if (line.section("=",-1).trimmed().section(nmbrs,-1,-1) == "w")
	      ui.cmbTimerSlack->setCurrentIndex(ui.cmbTimerSlack->findText("weeks"));
	  }

      } else if (line.contains(systemids[33])) {
	  if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	    QString content = line.section('=',1).trimmed();

	    while (!content.trimmed().isEmpty())
	    {
	      // If first character is quotation sign
	      if (content.trimmed().left(1) == "\"") {
		
		// read variable name and value
		QPair<QString,QString> i;
		i.first = content.section('\"',1,1).section('=',0,0);
		i.second = content.section('\"',1,1).section('=',-1);
		environ.append(i);
			
		// remove the read variable from content
		content = content.remove("\"" + content.section('\"',1,1) + "\"");
		
	      } else {  // if first character is not quotation sign
		QPair<QString,QString> i;
		
		i.first = content.section('=',0,0).trimmed();
		i.second = content.section('=',1,1).trimmed().section(QRegExp("\\s+"),0,0);
		environ.append(i);
		
		// remove the read variable from the string
		content = content.remove(content.section(QRegExp("\\s+"),0,0));
	      }
	      // Trim any extra whitespace
	      content = content.trimmed();
	    }

	  }	  
	 
      } // if line contains
      
      // default resource limits
      for (int i = 0; i <= 15; i++)
      {
	if (line.contains(systemids[i + 17]))
	{
	  if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty())
	    resLimits[QString(line.section('=',0,0).trimmed())] = line.section("=",-1).trimmed();
	  else
	    resLimits[systemids[i + 17]] = 0;
	}
      }

      line = in.readLine();
    } // read lines until empty
        
  } // if file open
  else 
    KMessageBox::error(this, i18n("Failed to read %1/system.conf. Using default values.", etcDir));
    
}

void kcmsystemd::readJournaldConf()
{
  // Set keywords for parsing journald.conf:
  QStringList journalids = QStringList() << "Storage" << "Compress" << "Seal"
    << "SplitMode" << "SyncIntervalSec" << "RateLimitInterval" << "RateLimitBurst"
    << "SystemMaxUse" << "SystemKeepFree" << "SystemMaxFileSize" << "RuntimeMaxUse"
    << "RuntimeKeepFree" << "RuntimeMaxFileSize" << "MaxRetentionSec" << "MaxFileSec"
    << "ForwardToSyslog" << "ForwardToKMsg" << "ForwardToConsole" << "TTYPath"
    << "MaxLevelStore" << "MaxLevelSyslog" << "MaxLevelKMsg" << "MaxLevelConsole";
  
  QFile journaldfile (etcDir + "/journald.conf");

  if (journaldfile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&journaldfile);
    QString line = in.readLine();

    while(!line.isNull()) {
    
      if (line.contains(journalids[0])) {
	if (line.trimmed().left(1) != "#" && !line.section("=",-1).trimmed().isEmpty())
	  ui.cmbStorage->setCurrentIndex(ui.cmbStorage->findText(line.section("=",-1).trimmed().toLower()));
      
      } else if (line.contains(journalids[1])) {
	if (line.trimmed().left(1) != "#" && !ToBoolDefOn(line.section('=',-1)))
	  ui.chkCompressLogs->setChecked(0);
	
      } else if (line.contains(journalids[2])) {
	if (line.trimmed().left(1) != "#" && !ToBoolDefOn(line.section('=',-1)))
	  ui.chkFwdSecureSealing->setChecked(0);
      
      } else if (line.contains(journalids[3])) {
	if (line.trimmed().left(1) != "#" && line.section('=',-1).trimmed().toLower() == "uid")
	  ui.radUID->setChecked(1);
	else if (line.trimmed().left(1) != "#" && line.section('=',-1).trimmed().toLower() == "none")
	  ui.radNone->setChecked(1);
	
      } else if (line.contains(journalids[4])) {
	ui.cmbSync->setCurrentIndex(ui.cmbSync->findText("minutes"));
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  QRegExp nmbrs("[0-9]+");
	  QRegExp timeunit("ms|s|min|h|d|w");
	  ui.spnSync->setValue(line.section("=",-1).trimmed().section(timeunit,0,0).toInt());
	  if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "ms") {
	    ui.cmbSync->setCurrentIndex(ui.cmbSync->findText("milliseconds"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "s" || line.section("=",-1).section(nmbrs,-1,-1).trimmed().isEmpty()) {
	    ui.cmbSync->setCurrentIndex(ui.cmbSync->findText("seconds"));    
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "min") {
	    ui.cmbSync->setCurrentIndex(ui.cmbSync->findText("minutes"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "h") {
	    ui.cmbSync->setCurrentIndex(ui.cmbSync->findText("hours"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "d") {
	    ui.cmbSync->setCurrentIndex(ui.cmbSync->findText("days"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "w") {
	    ui.cmbSync->setCurrentIndex(ui.cmbSync->findText("weeks"));
	  }
	}

      } else if (line.contains(journalids[5])) {
	ui.cmbRateInterval->setCurrentIndex(ui.cmbRateInterval->findText("seconds"));
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  QRegExp nmbrs("[0-9]+");
	  QRegExp timeunit("us|ms|s|min|h");
	  ui.spnRateInterval->setValue(line.section("=",-1).trimmed().section(timeunit,0,0).toInt());
	  if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "us") {
	      ui.cmbRateInterval->setCurrentIndex(ui.cmbRateInterval->findText("microseconds"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "ms") {
	      ui.cmbRateInterval->setCurrentIndex(ui.cmbRateInterval->findText("milliseconds"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "s") {
	      ui.cmbRateInterval->setCurrentIndex(ui.cmbRateInterval->findText("seconds"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "min") {
	      ui.cmbRateInterval->setCurrentIndex(ui.cmbRateInterval->findText("minutes"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "h") {
	      ui.cmbRateInterval->setCurrentIndex(ui.cmbRateInterval->findText("hours"));
	  }
	}
      
      } else if (line.contains(journalids[6])) {
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  ui.spnBurst->setValue(line.section("=",-1).trimmed().toInt());
	}

      } else if (line.contains(journalids[7])) {
        if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty())
	{ 
	  QRegExp nmbrs("[0-9]+");
	  QRegExp sizeF("k|m|g|t|p|e|K|M|G|T|P|E");
	  perDiskUsageValue = (line.section("=",-1).trimmed().section(sizeF,0,0).toFloat());
	  if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "K")
	    perDiskUsageValue = perDiskUsageValue / 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "M")
	    perDiskUsageValue = perDiskUsageValue * 1;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "G")
	    perDiskUsageValue = perDiskUsageValue * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "T")
	    perDiskUsageValue = perDiskUsageValue * 1024 * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "P")
	    perDiskUsageValue = perDiskUsageValue * 1024 * 1024 * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "E")
	    perDiskUsageValue = perDiskUsageValue * 1024 * 1024 * 1024 * 1024;
	  else
	    perDiskUsageValue = perDiskUsageValue / 1024 / 1024;
	  // only run if we are using persistent storage:
	  if (isPersistent)
	    ui.spnDiskUsage->setValue(perDiskUsageValue + 0.5);
	}
	
      } else if (line.contains(journalids[8])) {
        if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty())
	{ 
	  QRegExp nmbrs("[0-9]+");
	  QRegExp sizeF("k|m|g|t|p|e|K|M|G|T|P|E");
	  perDiskFreeValue = (line.section("=",-1).trimmed().section(sizeF,0,0).toFloat());
	  if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "K")
	    perDiskFreeValue = perDiskFreeValue / 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "M")
	    perDiskFreeValue = perDiskFreeValue * 1;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "G")
	    perDiskFreeValue = perDiskFreeValue * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "T")
	    perDiskFreeValue = perDiskFreeValue * 1024 * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "P")
	    perDiskFreeValue = perDiskFreeValue * 1024 * 1024 * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "E")
	    perDiskFreeValue = perDiskFreeValue * 1024 * 1024 * 1024 * 1024;
	  else
	    perDiskFreeValue = perDiskFreeValue / 1024 / 1024;
	  // only run if we are using persistent storage:
	  if (isPersistent)
	    ui.spnDiskFree->setValue(perDiskFreeValue + 0.5);
	}
	
      } else if (line.contains(journalids[9])) {
        if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty())
	{ 
	  QRegExp nmbrs("[0-9]+");
	  QRegExp sizeF("k|m|g|t|p|e|K|M|G|T|P|E");
	  perSizeFilesValue = (line.section("=",-1).trimmed().section(sizeF,0,0).toFloat());
	  if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "K")
	    perSizeFilesValue = perSizeFilesValue / 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "M")
	    perSizeFilesValue = perSizeFilesValue * 1;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "G")
	    perSizeFilesValue = perSizeFilesValue * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "T")
	    perSizeFilesValue = perSizeFilesValue * 1024 * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "P")
	    perSizeFilesValue = perSizeFilesValue * 1024 * 1024 * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "E")
	    perSizeFilesValue = perSizeFilesValue * 1024 * 1024 * 1024 * 1024;
	  else
	    perSizeFilesValue = perSizeFilesValue / 1024 / 1024;
	  // only run if we are using persistent storage:
	  if (isPersistent)
	    ui.spnSizeFiles->setValue(perSizeFilesValue + 0.5);
	}
	
      } else if (line.contains(journalids[10])) {
        if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty())
	{ 
	  QRegExp nmbrs("[0-9]+");
	  QRegExp sizeF("k|m|g|t|p|e|K|M|G|T|P|E");
	  volDiskUsageValue = (line.section("=",-1).trimmed().section(sizeF,0,0).toFloat());
	  if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "K")
	    volDiskUsageValue = volDiskUsageValue / 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "M")
	    volDiskUsageValue = volDiskUsageValue * 1;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "G")
	    volDiskUsageValue = volDiskUsageValue * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "T")
	    volDiskUsageValue = volDiskUsageValue * 1024 * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "P")
	    volDiskUsageValue = volDiskUsageValue * 1024 * 1024 * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "E")
	    volDiskUsageValue = volDiskUsageValue * 1024 * 1024 * 1024 * 1024;
	  else
	    volDiskUsageValue = volDiskUsageValue / 1024 / 1024;
	  // only run if we are using volatile storage:
	  if (!isPersistent)
	    ui.spnDiskUsage->setValue(volDiskUsageValue + 0.5);
	}
	
      } else if (line.contains(journalids[11])) {
        if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty())
	{ 
	  QRegExp nmbrs("[0-9]+");
	  QRegExp sizeF("k|m|g|t|p|e|K|M|G|T|P|E");
	  volDiskFreeValue = (line.section("=",-1).trimmed().section(sizeF,0,0).toFloat());
	  if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "K")
	    volDiskFreeValue = volDiskFreeValue / 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "M")
	    volDiskFreeValue = volDiskFreeValue * 1;	  
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "G")
	    volDiskFreeValue = volDiskFreeValue * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "T")
	    volDiskFreeValue = volDiskFreeValue * 1024 * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "P")
	    volDiskFreeValue = volDiskFreeValue * 1024 * 1024 * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "E")
	    volDiskFreeValue = volDiskFreeValue * 1024 * 1024 * 1024 * 1024;
	  else
	    volDiskFreeValue = volDiskFreeValue / 1024 / 1024;
	  // only run if we are using volatile storage:
	  if (!isPersistent)
	    ui.spnDiskFree->setValue(volDiskFreeValue + 0.5);
	}
	
      } else if (line.contains(journalids[12])) {
        if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty())
	{ 
	  QRegExp nmbrs("[0-9]+");
	  QRegExp sizeF("k|m|g|t|p|e|K|M|G|T|P|E");
	  volSizeFilesValue = (line.section("=",-1).trimmed().section(sizeF,0,0).toFloat());
	  if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "K")
	    volSizeFilesValue = volSizeFilesValue / 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "M")
	    volSizeFilesValue = volSizeFilesValue * 1;	  
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "G")
	    volSizeFilesValue = volSizeFilesValue * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "T")
	    volSizeFilesValue = volSizeFilesValue * 1024 * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "P")
	    volSizeFilesValue = volSizeFilesValue * 1024 * 1024 * 1024;
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toUpper() == "E")
	    volSizeFilesValue = volSizeFilesValue * 1024 * 1024 * 1024 * 1024;
	  else
	    volSizeFilesValue = volSizeFilesValue / 1024 / 1024;
	  // only run if we are using volatile storage:
	  if (!isPersistent)
	    ui.spnSizeFiles->setValue(volSizeFilesValue + 0.5);
	}
	
      } else if (line.contains(journalids[13])) {
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  ui.chkMaxRetention->setChecked(1);
	  QRegExp nmbrs("[0-9]+");
	  QRegExp timeunit("m|h|day|week|month|year");
	  ui.spnMaxRetention->setValue(line.section("=",-1).trimmed().section(timeunit,0,0).toInt());
	  if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "m") {
	      ui.cmbMaxRetention->setCurrentIndex(ui.cmbMaxRetention->findText("minutes"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "h") {
	      ui.cmbMaxRetention->setCurrentIndex(ui.cmbMaxRetention->findText("hours"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "day") {
	      ui.cmbMaxRetention->setCurrentIndex(ui.cmbMaxRetention->findText("days"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "week") {
	      ui.cmbMaxRetention->setCurrentIndex(ui.cmbMaxRetention->findText("weeks"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "month") {
	      ui.cmbMaxRetention->setCurrentIndex(ui.cmbMaxRetention->findText("months"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "year") {
	      ui.cmbMaxRetention->setCurrentIndex(ui.cmbMaxRetention->findText("years"));
	  }
	}
 
      } else if (line.contains(journalids[14])) {
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  ui.chkMaxFile->setChecked(1);
	  QRegExp nmbrs("[0-9]+");
	  QRegExp timeunit("m|h|day|week|month|year");
	  ui.spnMaxFile->setValue(line.section("=",-1).section(timeunit,0,0).trimmed().toInt());
	  if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "m") {
	      ui.cmbMaxFile->setCurrentIndex(ui.cmbMaxFile->findText("minutes"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "h") {
	      ui.cmbMaxFile->setCurrentIndex(ui.cmbMaxFile->findText("hours"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "day") {
	      ui.cmbMaxFile->setCurrentIndex(ui.cmbMaxFile->findText("days"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "week") {
	      ui.cmbMaxFile->setCurrentIndex(ui.cmbMaxFile->findText("weeks"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "month") {
	      ui.cmbMaxFile->setCurrentIndex(ui.cmbMaxFile->findText("months"));
	  } else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "year") {
	      ui.cmbMaxFile->setCurrentIndex(ui.cmbMaxFile->findText("years"));
	  }
	}
	
      } else if (line.contains(journalids[15])) {
	if (line.trimmed().left(1) != "#" && !ToBoolDefOn(line.section('=',-1)))
	  ui.chkFwdSyslog->setChecked(0);
	
      } else if (line.contains(journalids[16])) {
	if (line.trimmed().left(1) != "#" && ToBoolDefOff(line.section('=',-1)))
	  ui.chkFwdKmsg->setChecked(1);
	
      } else if (line.contains(journalids[17])) {
	if (line.trimmed().left(1) != "#" && ToBoolDefOff(line.section('=',-1)))
	{
	  ui.chkFwdConsole->setChecked(1);
	  ui.leTTYPath->setEnabled(1);
	}
	
      } else if (line.contains(journalids[18])) {
	  if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	    ui.leTTYPath->setText(line.section('=',-1).trimmed());
	  }
	
      } else if (line.contains(journalids[19])) {
	ui.cmbLevelStore->setCurrentIndex(7);
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  ui.cmbLevelStore->setCurrentIndex(ui.cmbLevelStore->findText(line.section("=",-1).trimmed().toLower()));
        }
        
      } else if (line.contains(journalids[20])) {
	ui.cmbLevelSyslog->setCurrentIndex(7);
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  ui.cmbLevelSyslog->setCurrentIndex(ui.cmbLevelSyslog->findText(line.section("=",-1).trimmed().toLower()));
        }
        
      } else if (line.contains(journalids[21])) {
	ui.cmbLevelKmsg->setCurrentIndex(5);
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  ui.cmbLevelKmsg->setCurrentIndex(ui.cmbLevelKmsg->findText(line.section("=",-1).trimmed().toLower()));
        }
        
      } else if (line.contains(journalids[22])) {
	ui.cmbLevelConsole->setCurrentIndex(6);
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  ui.cmbLevelConsole->setCurrentIndex(ui.cmbLevelConsole->findText(line.section("=",-1).trimmed().toLower()));
        }
        	
      } // if line contains...
      line = in.readLine();

    } // read lines until empty
  } // if file open 
  else 
    KMessageBox::error(this, i18n("Failed to read %1/journald.conf. Using default values.", etcDir));
  
}

void kcmsystemd::readLogindConf()
{
  // Set keywords for parsing logind.conf:
  QStringList loginids = QStringList() << "NAutoVTs" << "ReserveVT" << "KillUserProcesses"
  << "KillOnlyUsers" << "KillExcludeUsers" << "Controllers" << "ResetControllers" 
  << "InhibDelayMaxSec" << "HandlePowerKey" << "HandleSuspendKey" << "HandleHibernateKey" 
  << "HandleLidSwitch" << "PowerKeyIgnoreInhibited" << "SuspendKeyIgnoreInhibited" 
  << "HibernateKeyIgnoreInhibited" << "LidSwitchIgnoreInhibited" << "IdleAction=" 
  << "IdleActionSec";

  QFile file (etcDir + "/logind.conf");

  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&file);
    QString line = in.readLine();

    while(!line.isNull()) {
      
      if (line.contains(loginids[0])) {
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  ui.spnAutoVTs->setValue(line.section("=",-1).trimmed().toInt());
	}
	
      } else if (line.contains(loginids[1])) {
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  ui.spnReserveVT->setValue(line.section("=",-1).trimmed().toInt());
	}
	
      } else if (line.contains(loginids[2])) {
	if (line.trimmed().left(1) != "#" && ToBoolDefOff(line.section('=',-1)))
	  ui.chkKillUserProc->setChecked(1);
	
      } else if (line.contains(loginids[3])) {
	  if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	    ui.leKillOnlyUsers->setText(line.section('=',-1).trimmed());
	  }
	  
      } else if (line.contains(loginids[4])) {
	  if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	    ui.leKillExcludeUsers->setText(line.section('=',-1).trimmed());
	  }
	
      } else if (line.startsWith(loginids[5])) {
	  if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	    ui.leControllers->setText(line.section('=',-1).trimmed());
	  }
	  
      } else if (line.contains(loginids[6])) {
	  if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	    ui.leResetControllers->setText(line.section('=',-1).trimmed());
	  }
	
      } else if (line.contains(loginids[7])) {
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  ui.spnInhibDelayMax->setValue(line.section("=",-1).trimmed().toInt());
	}
	
      } else if (line.contains(loginids[8])) {
	ui.cmbPowerKey->setCurrentIndex(1);
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  ui.cmbPowerKey->setCurrentIndex(ui.cmbPowerKey->findText(line.section("=",-1).trimmed().toLower()));
        }
        
      } else if (line.contains(loginids[9])) {
	ui.cmbSuspendKey->setCurrentIndex(5);
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  ui.cmbSuspendKey->setCurrentIndex(ui.cmbSuspendKey->findText(line.section("=",-1).trimmed().toLower()));
        }
        
      } else if (line.contains(loginids[10])) {
	ui.cmbHibernateKey->setCurrentIndex(6);
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  ui.cmbHibernateKey->setCurrentIndex(ui.cmbHibernateKey->findText(line.section("=",-1).trimmed().toLower()));
        }
        
      } else if (line.contains(loginids[11])) {
	ui.cmbLidSwitch->setCurrentIndex(5);
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  ui.cmbLidSwitch->setCurrentIndex(ui.cmbLidSwitch->findText(line.section("=",-1).trimmed().toLower()));
        }

      } else if (line.contains(loginids[12])) {
	if (line.trimmed().left(1) != "#" && ToBoolDefOff(line.section('=',-1)))
	  ui.chkPowerKey->setChecked(1);
	
      } else if (line.contains(loginids[13])) {
	if (line.trimmed().left(1) != "#" && ToBoolDefOff(line.section('=',-1)))
	  ui.chkSuspendKey->setChecked(1);

      } else if (line.contains(loginids[14])) {
	if (line.trimmed().left(1) != "#" && ToBoolDefOff(line.section('=',-1)))
	  ui.chkHibernateKey->setChecked(1);
	
      } else if (line.contains(loginids[15])) {
	if (line.trimmed().left(1) != "#" && !ToBoolDefOn(line.section('=',-1)))
	  ui.chkLidSwitch->setChecked(0);

      } else if (line.contains(loginids[16])) {
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty())
	  ui.cmbIdleAction->setCurrentIndex(ui.cmbIdleAction->findText(line.section("=",-1).trimmed().toLower()));

      } else if (line.contains(loginids[17])) {
	if (line.trimmed().left(1) != "#" && !line.section('=',-1).trimmed().isEmpty()) {
	  QRegExp nmbrs("[0-9]+");
	  QRegExp timeunit("m|h");
	  ui.spnIdleActionSec->setValue(line.section("=",-1).section(timeunit,0,0).trimmed().toInt());
	  if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "m")
	      ui.cmbIdleActionSec->setCurrentIndex(ui.cmbIdleActionSec->findText("minutes"));
	  else if (line.section("=",-1).section(nmbrs,-1,-1).trimmed().toLower() == "h")
	      ui.cmbIdleActionSec->setCurrentIndex(ui.cmbIdleActionSec->findText("hours"));
	}

      } // if line contains...
      line = in.readLine();

    } // read lines until empty
  } // if file open 
  else 
    KMessageBox::error(this, i18n("Failed to read %1/logind.conf. Using default values.", etcDir));
  
} 

void kcmsystemd::defaults()
{
  if (KMessageBox::warningYesNo(this, i18n("Are you sure you want to load the default settings?")) == 3)
  { 
    //defaults for system.conf
    ui.cmbLogLevel->setCurrentIndex(ui.cmbLogLevel->findText("info"));
    ui.cmbLogTarget->setCurrentIndex(ui.cmbLogTarget->findText("journal"));
    ui.chkLogColor->setChecked(1);
    ui.chkLogLocation->setChecked(1);
    ui.chkDumpCore->setChecked(1);
    ui.chkCrashShell->setChecked(0);
    ui.chkShowStatus->setChecked(1);
    ui.cmbCrashVT->setCurrentIndex(0);
    ui.leCPUAffinity->setText("1 2");
    ui.chkCPUAffinity->setChecked(0);
    ui.leDefControllers->setText("cpu");
    ui.cmbDefStdOutput->setCurrentIndex(ui.cmbDefStdOutput->findText("journal"));
    ui.cmbDefStdError->setCurrentIndex(ui.cmbDefStdError->findText("inherit"));
    ui.leJoinControllers->setText("cpu,cpuacct");
    ui.spnRuntimeWatchdog->setValue(0);
    ui.cmbRuntimeWatchdog->setCurrentIndex(ui.cmbRuntimeWatchdog->findText("seconds"));
    ui.spnShutdownWatchdog->setValue(10);
    ui.cmbShutdownWatchdog->setCurrentIndex(ui.cmbShutdownWatchdog->findText("minutes"));
    ui.leCapBoundSet->setText("");
    ui.cmbTimerSlack->setCurrentIndex(ui.cmbTimerSlack->findText("nanoseconds"));
    ui.spnTimerSlack->setValue(0);
    for(QVariantMap::const_iterator iter = kcmsystemd::resLimits.begin(); iter != kcmsystemd::resLimits.end(); ++iter)
      kcmsystemd::resLimits[QString(iter.key())] = 0;
    environ.clear();
    
    //defaults for journald.conf
    ui.cmbStorage->setCurrentIndex(ui.cmbStorage->findText("auto"));
    ui.chkCompressLogs->setChecked(1);
    ui.chkFwdSecureSealing->setChecked(1);
    ui.radLogin->setChecked(1);
    ui.cmbSync->setCurrentIndex(ui.cmbSync->findText("minutes"));
    ui.spnSync->setValue(5);
    ui.cmbRateInterval->setCurrentIndex(ui.cmbRateInterval->findText("seconds"));
    ui.spnRateInterval->setValue(10);
    ui.spnBurst->setValue(200);
    if (QDir("/var/log/journal").exists())
    {
      ui.spnDiskUsage->setValue(0.1 * partPersSizeMB);
      ui.spnDiskFree->setValue(0.15 * partPersSizeMB);
      ui.spnSizeFiles->setValue(0.0125 * partPersSizeMB);
    }
    else
    {
      ui.spnDiskUsage->setValue(0.1 * partVolaSizeMB);
      ui.spnDiskFree->setValue(0.15 * partVolaSizeMB);
      ui.spnSizeFiles->setValue(0.0125 * partVolaSizeMB);
    }
    ui.chkMaxRetention->setChecked(0);
    ui.spnMaxRetention->setValue(0);
    ui.cmbMaxRetention->setCurrentIndex(ui.cmbMaxRetention->findText("seconds"));
    ui.chkMaxFile->setChecked(0);
    ui.spnMaxFile->setValue(0);
    ui.cmbMaxFile->setCurrentIndex(ui.cmbMaxFile->findText("seconds"));
    ui.chkFwdSyslog->setChecked(1);
    ui.chkFwdKmsg->setChecked(0);
    ui.chkFwdConsole->setChecked(0);
    ui.leTTYPath->setEnabled(0);
    ui.leTTYPath->setText("/dev/console");
    ui.cmbLevelStore->setCurrentIndex(ui.cmbLevelStore->findText("debug"));
    ui.cmbLevelSyslog->setCurrentIndex(ui.cmbLevelSyslog->findText("debug"));
    ui.cmbLevelKmsg->setCurrentIndex(ui.cmbLevelKmsg->findText("notice"));
    ui.cmbLevelConsole->setCurrentIndex(ui.cmbLevelConsole->findText("info"));
    
    //defaults for logind.conf
    ui.spnAutoVTs->setValue(6);
    ui.spnReserveVT->setValue(6);
    ui.chkKillUserProc->setChecked(0);
    ui.leKillOnlyUsers->setText("");
    ui.leKillExcludeUsers->setText("root");
    ui.leControllers->setText("");
    ui.leResetControllers->setText("cpu");
    ui.spnInhibDelayMax->setValue(5);
    ui.cmbPowerKey->setCurrentIndex(ui.cmbPowerKey->findText("poweroff"));
    ui.cmbSuspendKey->setCurrentIndex(ui.cmbSuspendKey->findText("suspend"));
    ui.cmbHibernateKey->setCurrentIndex(ui.cmbHibernateKey->findText("hibernate"));
    ui.cmbLidSwitch->setCurrentIndex(ui.cmbLidSwitch->findText("suspend"));
    ui.chkPowerKey->setChecked(0);
    ui.chkSuspendKey->setChecked(0);
    ui.chkHibernateKey->setChecked(0);
    ui.chkLidSwitch->setChecked(1);
    ui.cmbIdleAction->setCurrentIndex(ui.cmbIdleAction->findText("ignore"));
    ui.spnIdleActionSec->setValue(30);
    ui.cmbIdleActionSec->setCurrentIndex(ui.cmbIdleActionSec->findText("minutes"));
  }
}

void kcmsystemd::save()
{
  // prepare system.conf contents
  QString systemConfFileContents;
  systemConfFileContents.append("# " + etcDir + "/system.conf\n# Generated by kcmsystemd control module.\n");
  systemConfFileContents.append("[Manager]\n");
  systemConfFileContents.append("LogLevel=" + ui.cmbLogLevel->currentText() + "\n");
  systemConfFileContents.append("LogTarget=" + ui.cmbLogTarget->currentText() + "\n");
  if (ui.chkLogColor->isChecked())
    systemConfFileContents.append("LogColor=yes\n");
  else
    systemConfFileContents.append("LogColor=no\n");
  if (ui.chkLogLocation->isChecked())
    systemConfFileContents.append("LogLocation=yes\n");
  else
    systemConfFileContents.append("LogLocation=no\n");
  if (ui.chkDumpCore->isChecked())
    systemConfFileContents.append("DumpCore=yes\n");
  else
    systemConfFileContents.append("DumpCore=no\n");
  if (ui.chkCrashShell->isChecked())
    systemConfFileContents.append("CrashShell=yes\n");
  else
    systemConfFileContents.append("CrashShell=no\n");
  if (ui.chkShowStatus->isChecked())
    systemConfFileContents.append("ShowStatus=yes\n");
  else
    systemConfFileContents.append("ShowStatus=no\n");
  if (ui.cmbCrashVT->currentText() == "Off")
    systemConfFileContents.append("CrashChVT=-1\n");
  else
    systemConfFileContents.append("CrashChVT=" + ui.cmbCrashVT->currentText() + "\n");
  if (ui.chkCPUAffinity->isChecked())
    systemConfFileContents.append("CPUAffinity=" + ui.leCPUAffinity->text() + "\n");
  else
    systemConfFileContents.append("CPUAffinity=\n");
  systemConfFileContents.append("DefaultControllers=" + ui.leDefControllers->text() + "\n");
  systemConfFileContents.append("DefaultStandardOutput=" + ui.cmbDefStdOutput->currentText() + "\n");
  systemConfFileContents.append("DefaultStandardError=" + ui.cmbDefStdError->currentText() + "\n");
  systemConfFileContents.append("JoinControllers=" + ui.leJoinControllers->text() + "\n");
  systemConfFileContents.append("RuntimeWatchdogSec=" + ui.spnRuntimeWatchdog->cleanText());
  switch (ui.cmbRuntimeWatchdog->currentIndex())
  {
  case 0:
    systemConfFileContents.append("ms\n");
    break;
  case 1:
    systemConfFileContents.append("\n");
    break;
  case 2:
    systemConfFileContents.append("min\n");
    break;
  case 3:
    systemConfFileContents.append("h\n");
    break;
  case 4:
    systemConfFileContents.append("d\n");
    break;
  case 5:
    systemConfFileContents.append("w\n");
  }
  systemConfFileContents.append("ShutdownWatchdogSec=" + ui.spnShutdownWatchdog->cleanText());
  switch (ui.cmbShutdownWatchdog->currentIndex())
  {
  case 0:
    systemConfFileContents.append("ms\n");
    break;
  case 1:
    systemConfFileContents.append("\n");
    break;
  case 2:
    systemConfFileContents.append("min\n");
    break;
  case 3:
    systemConfFileContents.append("h\n");
    break;
  case 4:
    systemConfFileContents.append("d\n");
    break;
  case 5:
    systemConfFileContents.append("w\n");
  } 
  systemConfFileContents.append("CapabilityBoundingSet=" + ui.leCapBoundSet->text() + "\n");
  systemConfFileContents.append("TimerSlackNSec=" + ui.spnTimerSlack->cleanText());
  switch (ui.cmbTimerSlack->currentIndex())
  {
  case 0:
    systemConfFileContents.append("\n");
    break;
  case 1:
    systemConfFileContents.append("ms\n");
    break;
  case 2:
    systemConfFileContents.append("s\n");
    break;
  case 3:
    systemConfFileContents.append("min\n");
    break;
  case 4:
    systemConfFileContents.append("h\n");
    break;
  case 5:
    systemConfFileContents.append("d\n");
    break;
  case 6:
    systemConfFileContents.append("w\n");
  }
  
  for(QVariantMap::const_iterator iter = resLimits.begin(); iter != resLimits.end(); ++iter)
  {
    if (iter.value() == 0)
      systemConfFileContents.append(iter.key() + "=infinity\n");
    else
      systemConfFileContents.append(iter.key() + "=" + iter.value().toString() + "\n");
  }
  
  systemConfFileContents.append("DefaultEnvironment=");
  QListIterator<QPair<QString,QString> > i(kcmsystemd::environ);
  while (i.hasNext())
  {
    if (i.peekNext().first.contains(" ") || i.peekNext().second.contains(" "))
    {
      systemConfFileContents.append("\"" + i.peekNext().first + "=" + i.peekNext().second + "\" ");
      i.next();
    } else {
      systemConfFileContents.append(i.peekNext().first + "=" + i.peekNext().second + " ");
      i.next();
    }
  }
   
  // QMessageBox::information(this, "system.conf contents", systemConfFileContents);
  
  // prepare journald.conf contents
  QString journaldConfFileContents;
  journaldConfFileContents.append("# " + etcDir + "/journal.conf\n# Generated by kcmsystemd control module.\n");
  journaldConfFileContents.append("[Journal]\n");
  journaldConfFileContents.append("Storage=" + ui.cmbStorage->currentText() + "\n");
  if (ui.chkCompressLogs->isChecked())
    journaldConfFileContents.append("Compress=yes\n");
  else
    journaldConfFileContents.append("Compress=no\n");
  if (ui.chkFwdSecureSealing->isChecked())
    journaldConfFileContents.append("Seal=yes\n");
  else
    journaldConfFileContents.append("Seal=no\n");
  if (ui.radLogin->isChecked())
    journaldConfFileContents.append("SplitMode=login\n");
  else if (ui.radUID->isChecked())
    journaldConfFileContents.append("SplitMode=uid\n");
  else if (ui.radNone->isChecked())
    journaldConfFileContents.append("SplitMode=none\n");
  journaldConfFileContents.append("SyncIntervalSec=" + ui.spnSync->cleanText());
  switch (ui.cmbSync->currentIndex())
  {
  case 0:
    journaldConfFileContents.append("\n");
    break;
  case 1:
    journaldConfFileContents.append("m\n");
    break;
  case 2:
    journaldConfFileContents.append("h\n");
    break;
  case 3:
    journaldConfFileContents.append("d\n");
    break;
  case 4:
    journaldConfFileContents.append("w\n");
    break;
  }
  journaldConfFileContents.append("RateLimitInterval=" + ui.spnRateInterval->cleanText());
  switch (ui.cmbRateInterval->currentIndex())
  {
  case 0:
    journaldConfFileContents.append("us\n");
    break;
  case 1:
    journaldConfFileContents.append("ms\n");
    break;
  case 2:
    journaldConfFileContents.append("s\n");
    break;
  case 3:
    journaldConfFileContents.append("m\n");
    break;
  case 4:
    journaldConfFileContents.append("h\n");
    break;
  }
  journaldConfFileContents.append("RateLimitBurst=" + ui.spnBurst->cleanText() + "\n");
  // if  (ui.cmbStorage->currentIndex() == 1 || (ui.cmbStorage->currentIndex() == 2 && QDir("/var/log/journal").exists()))
  if  (isPersistent)
    journaldConfFileContents.append("SystemMaxUse=");
  else if (ui.cmbStorage->currentIndex() == 0 || (ui.cmbStorage->currentIndex() == 2 && !QDir("/var/log/journal").exists()))
    journaldConfFileContents.append("RuntimeMaxUse=");
  journaldConfFileContents.append(ui.spnDiskUsage->cleanText() + "M\n");
  if  (ui.cmbStorage->currentIndex() == 1 || (ui.cmbStorage->currentIndex() == 2 && QDir("/var/log/journal").exists()))
    journaldConfFileContents.append("SystemKeepFree=");
  else if (ui.cmbStorage->currentIndex() == 0 || (ui.cmbStorage->currentIndex() == 2 && !QDir("/var/log/journal").exists()))
    journaldConfFileContents.append("RuntimeKeepFree=");
  journaldConfFileContents.append(ui.spnDiskFree->cleanText() + "M\n");
  if (ui.cmbStorage->currentIndex() == 1 || (ui.cmbStorage->currentIndex() == 2 && QDir("/var/log/journal").exists()))
    journaldConfFileContents.append("SystemMaxFileSize=");
  else if (ui.cmbStorage->currentIndex() == 0 || (ui.cmbStorage->currentIndex() == 2 && !QDir("/var/log/journal").exists()))
    journaldConfFileContents.append("RuntimeMaxFileSize=");
  journaldConfFileContents.append(ui.spnSizeFiles->cleanText() + "M\n");
  if (ui.chkMaxRetention->isChecked()) {
    journaldConfFileContents.append("MaxRetentionSec=" + ui.spnMaxRetention->cleanText());
    switch (ui.cmbMaxRetention->currentIndex())
    {
    case 0:
      journaldConfFileContents.append("\n");
      break;
    case 1:
      journaldConfFileContents.append("m\n");
      break;
    case 2:
      journaldConfFileContents.append("h\n");
      break;
    case 3:
      journaldConfFileContents.append("day\n");
      break;
    case 4:
      journaldConfFileContents.append("week\n");
      break;
    case 5:
      journaldConfFileContents.append("month\n");
      break;
    case 6:
      journaldConfFileContents.append("year\n");
      break;
    }
  }
  if (ui.chkMaxFile->isChecked()) {
    journaldConfFileContents.append("MaxFileSec=" + ui.spnMaxFile->cleanText());
    switch (ui.cmbMaxFile->currentIndex())
    {
    case 0:
      journaldConfFileContents.append("\n");
      break;
    case 1:
      journaldConfFileContents.append("m\n");
      break;
    case 2:
      journaldConfFileContents.append("h\n");
      break;
    case 3:
      journaldConfFileContents.append("day\n");
      break;
    case 4:
      journaldConfFileContents.append("week\n");
      break;
    case 5:
      journaldConfFileContents.append("month\n");
      break;
    case 6:
      journaldConfFileContents.append("year\n");
      break;
    }
  }
  if (ui.chkFwdSyslog->isChecked())
    journaldConfFileContents.append("ForwardToSyslog=yes\n");
  else
    journaldConfFileContents.append("ForwardToSyslog=no\n");
  if (ui.chkFwdKmsg->isChecked())
    journaldConfFileContents.append("ForwardToKMsg=yes\n");
  else
    journaldConfFileContents.append("ForwardToKMsg=no\n");
  if (ui.chkFwdConsole->isChecked())
    journaldConfFileContents.append("ForwardToConsole=yes\n");
  else
    journaldConfFileContents.append("ForwardToConsole=no\n");  
  journaldConfFileContents.append("TTYPath=" + ui.leTTYPath->text() + "\n");
  journaldConfFileContents.append("MaxLevelStore=" + ui.cmbLevelStore->currentText() + "\n");
  journaldConfFileContents.append("MaxLevelSyslog=" + ui.cmbLevelSyslog->currentText() + "\n");
  journaldConfFileContents.append("MaxLevelKMsg=" + ui.cmbLevelKmsg->currentText() + "\n");
  journaldConfFileContents.append("MaxLevelConsole=" + ui.cmbLevelConsole->currentText() + "\n");
  
  //QMessageBox::information(this, "journald.conf contents", journaldConfFileContents);
  
  
  // prepare logind.conf contents
  QString logindConfFileContents;
  logindConfFileContents.append("# " + etcDir + "/logind.conf\n# Generated by kcmsystemd control module.\n");
  logindConfFileContents.append("[Login]\n");
  logindConfFileContents.append("NAutoVTs=" + ui.spnAutoVTs->cleanText() + "\n");
  logindConfFileContents.append("ReserveVT=" + ui.spnReserveVT->cleanText() + "\n");
  if (ui.chkKillUserProc->isChecked())
    logindConfFileContents.append("KillUserProcesses=yes\n");
  else
    logindConfFileContents.append("KillUserProcesses=no\n");
  logindConfFileContents.append("KillOnlyUsers=" + ui.leKillOnlyUsers->text() + "\n");
  logindConfFileContents.append("KillExcludeUsers=" + ui.leKillExcludeUsers->text() + "\n");
  logindConfFileContents.append("Controllers=" + ui.leControllers->text() + "\n");
  logindConfFileContents.append("ResetControllers=" + ui.leResetControllers->text() + "\n");
  logindConfFileContents.append("InhibDelayMaxSec=" + ui.spnInhibDelayMax->cleanText() + "\n");
  logindConfFileContents.append("HandlePowerKey=" + ui.cmbPowerKey->currentText() + "\n");
  logindConfFileContents.append("HandleSuspendKey=" + ui.cmbSuspendKey->currentText() + "\n");
  logindConfFileContents.append("HandleHibernateKey=" + ui.cmbHibernateKey->currentText() + "\n");
  logindConfFileContents.append("HandleLidSwitch=" + ui.cmbLidSwitch->currentText() + "\n");
  if (ui.chkPowerKey->isChecked())
    logindConfFileContents.append("PowerKeyIgnoreInhibited=yes\n");
  else
    logindConfFileContents.append("PowerKeyIgnoreInhibited=no\n");
  if (ui.chkSuspendKey->isChecked())
    logindConfFileContents.append("SuspendKeyIgnoreInhibited=yes\n");
  else
    logindConfFileContents.append("SuspendKeyIgnoreInhibited=no\n");
  if (ui.chkHibernateKey->isChecked())
    logindConfFileContents.append("HibernateKeyIgnoreInhibited=yes\n");
  else
    logindConfFileContents.append("HibernateKeyIgnoreInhibited=no\n");
  if (ui.chkLidSwitch->isChecked())
    logindConfFileContents.append("LidSwitchIgnoreInhibited=yes\n");
  else
    logindConfFileContents.append("LidSwitchIgnoreInhibited=no\n");
  logindConfFileContents.append("IdleAction=" + ui.cmbIdleAction->currentText() + "\n");
  logindConfFileContents.append("IdleActionSec=" + ui.spnIdleActionSec->cleanText());
  switch (ui.cmbIdleActionSec->currentIndex())
  {
  case 0:
    logindConfFileContents.append("\n");
    break;
  case 1:
    logindConfFileContents.append("m\n");
    break;
  case 2:
    logindConfFileContents.append("h\n");
    break;
  }  
  
  // Declare a QVariantMap with arguments for the helper
  QVariantMap helperArgs;
  if (QDir(etcDir).exists()) {
    helperArgs["etcDir"] = etcDir;
  } else {
    // Failed to find systemd config directory
    KMessageBox::error(this, i18n("Unable to find directory for configuration files."));
    return;
  }
  // QMessageBox::information(this, QString("Title"), helperArgs["etcDir"].toString() + "/system.conf");
  helperArgs["systemConfFileContents"] = systemConfFileContents;
  helperArgs["journaldConfFileContents"] = journaldConfFileContents;
  helperArgs["logindConfFileContents"] = logindConfFileContents;
  
  // helperArgs["sysfilename"] = "/home/ragnar/projects/kcmsystemd/confwrite/test.conf";
  
  // Call the helper to write the configuration files
  Action *saveAction = authAction();      
  saveAction->setArguments(helperArgs);
  ActionReply reply = saveAction->execute();
  
  // Respond to reply of the helper
  if(reply.failed())
  {
    // Writing the configuration files failed
    if (reply.type() == ActionReply::KAuthError)
    {
      // Authorization error
      KMessageBox::error(this, i18n("Unable to authenticate/execute the action: code %1", reply.errorCode()));
    }
    else 
    {
      // Other error
      KMessageBox::error(this, i18n("Unable to write the (%1) file:\n%2", reply.data()["filename"].toString(), reply.data()["errorDescription"].toString()));
    }
  } else {
    // Writing succeeded
    KMessageBox::information(this, i18n("Configuration files succesfully written to: %1", helperArgs["etcDir"].toString()));
  }
}

void kcmsystemd::slotDefaultChanged()
{
  emit changed(true);
}

void kcmsystemd::slotCPUAffinityChanged()
{
    if ( ui.chkCPUAffinity->isChecked()) {
      ui.leCPUAffinity->setEnabled(1);
    } else {
      ui.leCPUAffinity->setEnabled(0);
    }
    emit changed(true);
}

void kcmsystemd::slotStorageChanged()
{
  // no storage of logs
  if ( ui.cmbStorage->currentIndex() == 3) {
    ui.grpSizeRotation->setEnabled(0);
    ui.grpTimeRotation->setEnabled(0);
    ui.grpSplitLogFiles->setEnabled(0);
  // storage of logs
  } else {
    ui.grpSizeRotation->setEnabled(1);
    ui.grpTimeRotation->setEnabled(1);
    ui.grpSplitLogFiles->setEnabled(1);

    // using persistent storage:
    if (ui.cmbStorage->currentIndex() == 1 || (ui.cmbStorage->currentIndex() == 2 && QDir("/var/log/journal").exists())) {
      isPersistent = true;
      ui.spnDiskUsage->setMaximum(partPersSizeMB);
      ui.spnDiskUsage->setValue(perDiskUsageValue + 0.5);
      ui.spnDiskFree->setMaximum(partPersSizeMB);
      ui.spnDiskFree->setValue(perDiskFreeValue + 0.5);
      ui.spnSizeFiles->setMaximum(partPersSizeMB);
      ui.spnSizeFiles->setValue(perSizeFilesValue + 0.5);
    // using volatile storage:
    } else if (ui.cmbStorage->currentIndex() == 0 || (ui.cmbStorage->currentIndex() == 2 && !QDir("/var/log/journal").exists())) {
      isPersistent = false;
      ui.spnDiskUsage->setValue(volDiskUsageValue + 0.5);
      ui.spnDiskUsage->setMaximum(partVolaSizeMB);
      ui.spnDiskFree->setValue(volDiskFreeValue + 0.5);
      ui.spnDiskFree->setMaximum(partVolaSizeMB);
      ui.spnSizeFiles->setValue(volSizeFilesValue + 0.5);
      ui.spnSizeFiles->setMaximum(partVolaSizeMB);
    }
  }
  emit changed(true);
}


void kcmsystemd::slotSpnDiskUsageChanged()
{
  if (isPersistent)
    perDiskUsageValue = ui.spnDiskUsage->value();
  else
    volDiskUsageValue = ui.spnDiskUsage->value();
  emit changed(true);
}

void kcmsystemd::slotSpnDiskFreeChanged()
{
  if (isPersistent)
    perDiskFreeValue = ui.spnDiskFree->value();
  else
    volDiskFreeValue = ui.spnDiskFree->value();
  emit changed(true);
}

void kcmsystemd::slotSpnSizeFilesChanged()
{
  if (isPersistent)
    perSizeFilesValue = ui.spnSizeFiles->value();
  else
    volSizeFilesValue = ui.spnSizeFiles->value();
  emit changed(true);
}

void kcmsystemd::slotMaxRetentionChanged()
{
  if ( ui.chkMaxRetention->isChecked()) {
    ui.spnMaxRetention->setEnabled(1);
    ui.cmbMaxRetention->setEnabled(1);
  } else {
    ui.spnMaxRetention->setEnabled(0);
    ui.cmbMaxRetention->setEnabled(0);
  }
  emit changed(true);
}

void kcmsystemd::slotMaxFileChanged()
{
  if ( ui.chkMaxFile->isChecked()) {
    ui.spnMaxFile->setEnabled(1);
    ui.cmbMaxFile->setEnabled(1);
  } else {
    ui.spnMaxFile->setEnabled(0);
    ui.cmbMaxFile->setEnabled(0);
  }
  emit changed(true);
}

void kcmsystemd::slotFwdToSyslogChanged()
{
  if ( ui.chkFwdSyslog->isChecked())
    ui.cmbLevelSyslog->setEnabled(1);
  else
    ui.cmbLevelSyslog->setEnabled(0);
  emit changed(true);
}

void kcmsystemd::slotFwdToKmsgChanged()
{
  if ( ui.chkFwdKmsg->isChecked())
    ui.cmbLevelKmsg->setEnabled(1);
  else
    ui.cmbLevelKmsg->setEnabled(0);
  emit changed(true);
}

void kcmsystemd::slotFwdToConsoleChanged()
{
  if ( ui.chkFwdConsole->isChecked()) {
    ui.leTTYPath->setEnabled(1);
    ui.cmbLevelConsole->setEnabled(1);
  } else {
    ui.leTTYPath->setEnabled(0);
    ui.cmbLevelConsole->setEnabled(0);
  }
  emit changed(true);
}

void kcmsystemd::slotVersion()
{
  QByteArray version = pkgConfigVer->readAllStandardOutput();
  if (QString(version).toInt() < 205)
    ui.btnEnviron->setEnabled(0);
}

void kcmsystemd::slotKillUserProcessesChanged()
{
  if ( ui.chkKillUserProc->isChecked()) {
    ui.leKillOnlyUsers->setEnabled(1);
    ui.leKillExcludeUsers->setEnabled(1);
  } else {
    ui.leKillOnlyUsers->setEnabled(0);
    ui.leKillExcludeUsers->setEnabled(0);
  }
  emit changed(true);
}

void kcmsystemd::slotOpenResourceLimits()
{
  QPointer<ResLimitsDialog> resDialog = new ResLimitsDialog(this);
  resDialog->exec();
  delete resDialog;
  if (resLimitsChanged)
    emit changed(true);
}

void kcmsystemd::slotOpenEnviron()
{
  QPointer<EnvironDialog> environDialog = new EnvironDialog(this);
  environDialog->exec();
  delete environDialog;
  if (environChanged)
    emit changed(true);
}

bool kcmsystemd::ToBoolDefOff(QString astring)
{
  if (astring.trimmed().toLower() == "yes" || astring.trimmed().toLower() == "true" || astring.trimmed().toLower() == "on"|| astring.trimmed() == "1")
    return 1;
  else
    return 0;
}

bool kcmsystemd::ToBoolDefOn(QString astring)
{
  if (astring.trimmed().toLower() == "no" || astring.trimmed().toLower() == "false" || astring.trimmed().toLower() == "off"|| astring.trimmed() == "0")
    return 0;
  else
    return 1;
}
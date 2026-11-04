; SPDX-License-Identifier: GPL-3.0-or-later
;
; Windows x64 安装包的 Inno Setup 脚本。不手工调用：scripts/package.ps1 经环境变量传入下面四项再编译
; （走环境变量而不是 /D：路径里的反斜杠与冒号不必操心 ISPP 的引号规则）。
;   MICIRI_ISS_VERSION  工程版本（x.y.z）          MICIRI_ISS_STAGE    暂存目录（里面有 vst3\ 与 app\）
;   MICIRI_ISS_OUTDIR   安装包输出目录             MICIRI_ISS_OUTNAME  不带扩展名的文件名
; 布局：
;   {code:Vst3Dir}\Miciri.vst3    VST3 插件，缺省装进标准目录 Common Files\VST3；
;                                 命令行 /Vst3Dir=<目录> 可改（只给自动化验证用，向导里不让选）
;   {app}                         Program Files\Miciri：许可全文、第三方声明、INSTALL.txt 和卸载程序
; 不装别的东西：没有自动更新、没有遥测、没有卸载问卷。WebView2 运行时不随包安装（见 INSTALL.txt）。
#define MiciriVersion GetEnv("MICIRI_ISS_VERSION")
#define StageDir GetEnv("MICIRI_ISS_STAGE")
#define OutDir GetEnv("MICIRI_ISS_OUTDIR")
#define OutName GetEnv("MICIRI_ISS_OUTNAME")
#if MiciriVersion == "" || StageDir == "" || OutDir == "" || OutName == ""
  #error The MICIRI_ISS_* environment variables are not set: this script is driven by scripts/package.ps1
#endif

[Setup]
AppId={{6F8C6F33-594B-4D21-817E-FEEC279821D2}
AppName=Miciri
AppVersion={#MiciriVersion}
AppVerName=Miciri {#MiciriVersion}
AppPublisher=Synchain
AppPublisherURL=https://github.com/synchain-oss/miciri
AppSupportURL=https://github.com/synchain-oss/miciri
VersionInfoVersion={#MiciriVersion}.0
VersionInfoCompany=Synchain
VersionInfoProductName=Miciri
VersionInfoDescription=Miciri installer
DefaultDirName={commonpf64}\Miciri
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
LicenseFile={#StageDir}\app\LICENSE
OutputDir={#OutDir}
OutputBaseFilename={#OutName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no

[Files]
Source: "{#StageDir}\vst3\Miciri.vst3\*"; DestDir: "{code:Vst3Dir}\Miciri.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#StageDir}\app\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Code]
function Vst3Dir(Param: String): String;
begin
  Result := ExpandConstant('{param:Vst3Dir|}');
  if Result = '' then
    Result := ExpandConstant('{commoncf64}\VST3');
end;

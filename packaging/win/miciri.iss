; SPDX-License-Identifier: GPL-3.0-or-later
;
; Windows x64 安装包的 Inno Setup 脚本。不手工调用：scripts/package.ps1 经 /D 传入下面四个变量并编译。
;   MiciriVersion  工程版本（x.y.z）          StageDir  暂存目录（里面有 vst3\ 与 app\）
;   OutDir         安装包输出目录             OutName   不带扩展名的文件名
; 布局：
;   {code:Vst3Dir}\Miciri.vst3    VST3 插件，缺省装进标准目录 Common Files\VST3；
;                                 命令行 /Vst3Dir=<目录> 可改（只给自动化验证用，向导里不让选）
;   {app}                         Program Files\Miciri：许可全文、第三方声明、INSTALL.txt 和卸载程序
; 不装别的东西：没有自动更新、没有遥测、没有卸载问卷。WebView2 运行时不随包安装（见 INSTALL.txt）。
#ifndef MiciriVersion
  #error MiciriVersion is not defined: this script is driven by scripts/package.ps1
#endif
#ifndef StageDir
  #error StageDir is not defined
#endif
#ifndef OutDir
  #error OutDir is not defined
#endif
#ifndef OutName
  #error OutName is not defined
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

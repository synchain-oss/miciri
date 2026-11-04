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
; 按系统界面语言自动选：LanguageDetectionMethod=uilanguage 取与界面语言匹配的语言（中文界面选简体中文）；
; ShowLanguageDialog=no 在匹配不上时不弹选择框，直接用第一项（英文）。命令行 /LANG=<语言名> 可强制指定。
; 许可协议页仍是 GPL 英文原文：LicenseFile 没有按语言区分。
LanguageDetectionMethod=uilanguage
ShowLanguageDialog=no
RestartApplications=no

[Languages]
; 英文是第一项，也是系统界面语言匹配不上时的回退。简体中文用 Inno Setup 官方仓库 Unofficial 目录里的翻译，
; 原样提交在 languages\，哈希钉在 ChineseSimplified.isl.sha256（来源见 languages\SOURCE.txt）。
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "languages\ChineseSimplified.isl"

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

{ 把生效的向导语言，和「下一步」按钮文字前三个字符的编码写进 /LOG 的日志：scripts/package.ps1 -Verify 据此核对
  中文向导真的编进了安装包、翻译文件按 UTF-8 读对了（用编码而不是文字本身，免得日志的编码影响比对）。 }
function InitializeSetup(): Boolean;
var
  Next: String;
begin
  Next := SetupMessage(msgButtonNext);
  Log('Miciri: language=' + ActiveLanguage + ' next=' + IntToStr(Ord(Next[1])) + ',' + IntToStr(Ord(Next[2])) + ',' + IntToStr(Ord(Next[3])));
  Result := True;
end;

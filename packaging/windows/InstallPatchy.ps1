[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PayloadZip,

    [string]$Version = "0.0.0",

    [switch]$Quiet,

    # The exact install folder. Without it, setup offers the folder of the existing
    # install, or %LOCALAPPDATA%\Programs\Patchy.
    [string]$InstallDir = "",

    # One-click update from a running Patchy (docs/auto-update.md): no wizard. Waits for
    # -WaitForProcessId to exit, installs into -InstallDir, leaves the desktop shortcut as
    # it is, and starts Patchy again. InstallPatchyLauncher.cs passes these when Patchy
    # set PATCHY_UPDATE_INSTALL_DIR.
    [switch]$Update,

    [int]$WaitForProcessId = 0,

    # Internal to -Update: the copy of this script that does the install while the first
    # copy shows the progress window.
    [switch]$UpdateWorker,

    # Packaging check: run the update-mode helpers (process wait, locked-file detection)
    # against cases built inside this scratch folder and exit without installing. Used by
    # scripts\release\verify-windows-package.ps1.
    [string]$UpdateCheckRoot = "",

    # Packaging check: load the wizard logo from the Patchy.ico beside the payload and exit.
    [switch]$CheckLogo,

    # Packaging check: build the whole wizard, show it invisibly, close it, and exit
    # without installing. Used by scripts\release\verify-windows-package.ps1.
    [switch]$SmokeTest,

    # Packaging check: write the "Open with" registration under this scratch registry key
    # instead of HKCU:\Software\Classes and exit without installing. Used by
    # scripts\release\verify-windows-package.ps1.
    [string]$OpenWithCheckRoot = "",

    # Packaging check: run the install folder rules against cases built inside this
    # scratch folder and exit without installing. Used by
    # scripts\release\verify-windows-package.ps1.
    [string]$LocationCheckRoot = ""
)

$ErrorActionPreference = "Stop"

function ConvertFrom-PatchyUnicodeEscapes {
    param([Parameter(Mandatory = $true)][string]$Text)

    return [regex]::Replace($Text, "\\u([0-9A-Fa-f]{4})", {
        param($Match)
        [string][char][Convert]::ToInt32($Match.Groups[1].Value, 16)
    })
}

$PatchyInstallerText = @{
    en = @{
        RunningPatchyRetryMessage = "Patchy is currently running. Save your work, close Patchy, then click Retry to continue installation."
        RunningPatchyQuietMessage = "Patchy is currently running. Close Patchy and run setup again."
        FileInUseQuietMessage = "Patchy could not be updated because installed files are in use. Close Patchy and run setup again."
        LockedFileRetryMessage = "Setup cannot replace {0} because another program is using it. Close that program, then click Retry to continue."
        InstallationCanceled = "Installation canceled."
        InstallFolderInvalid = "Choose a full folder path for Patchy, such as D:\Apps\Patchy. A drive root or the Windows folder cannot be used."
        InstallFolderNotEmpty = "The folder {0} already contains other files. Choose an empty folder or a new folder name, so that uninstalling Patchy never removes anything that is not its own."
        InstallFolderNotWritable = "Setup cannot write to {0}. This installer does not use administrator rights, so choose a folder you can write to, such as one in your user folder or on another drive."
    }
    ja = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u306F\u73FE\u5728\u5B9F\u884C\u4E2D\u3067\u3059\u3002\u4F5C\u696D\u3092\u4FDD\u5B58\u3057\u3066 Patchy \u3092\u9589\u3058\u3066\u304B\u3089\u3001[\u518D\u8A66\u884C] \u3092\u30AF\u30EA\u30C3\u30AF\u3057\u3066\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u3092\u7D9A\u884C\u3057\u3066\u304F\u3060\u3055\u3044\u3002"
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u306F\u73FE\u5728\u5B9F\u884C\u4E2D\u3067\u3059\u3002Patchy \u3092\u9589\u3058\u3066\u304B\u3089\u30BB\u30C3\u30C8\u30A2\u30C3\u30D7\u3092\u3082\u3046\u4E00\u5EA6\u5B9F\u884C\u3057\u3066\u304F\u3060\u3055\u3044\u3002"
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u6E08\u307F\u306E\u30D5\u30A1\u30A4\u30EB\u304C\u4F7F\u7528\u4E2D\u306E\u305F\u3081\u3001Patchy \u3092\u66F4\u65B0\u3067\u304D\u307E\u305B\u3093\u3067\u3057\u305F\u3002Patchy \u3092\u9589\u3058\u3066\u304B\u3089\u30BB\u30C3\u30C8\u30A2\u30C3\u30D7\u3092\u3082\u3046\u4E00\u5EA6\u5B9F\u884C\u3057\u3066\u304F\u3060\u3055\u3044\u3002"
        LockedFileRetryMessage = ConvertFrom-PatchyUnicodeEscapes "{0} \u306F\u5225\u306E\u30D7\u30ED\u30B0\u30E9\u30E0\u3067\u4F7F\u7528\u3055\u308C\u3066\u3044\u308B\u305F\u3081\u3001\u7F6E\u304D\u63DB\u3048\u308B\u3053\u3068\u304C\u3067\u304D\u307E\u305B\u3093\u3002\u305D\u306E\u30D7\u30ED\u30B0\u30E9\u30E0\u3092\u9589\u3058\u3066\u304B\u3089\u3001[\u518D\u8A66\u884C] \u3092\u30AF\u30EA\u30C3\u30AF\u3057\u3066\u7D9A\u884C\u3057\u3066\u304F\u3060\u3055\u3044\u3002"
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u306F\u30AD\u30E3\u30F3\u30BB\u30EB\u3055\u308C\u307E\u3057\u305F\u3002"
        InstallFolderInvalid = ConvertFrom-PatchyUnicodeEscapes "Patchy \u306E\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u5148\u3068\u3057\u3066\u3001D:\Apps\Patchy \u306E\u3088\u3046\u306A\u5B8C\u5168\u306A\u30D5\u30A9\u30EB\u30C0\u30FC \u30D1\u30B9\u3092\u6307\u5B9A\u3057\u3066\u304F\u3060\u3055\u3044\u3002\u30C9\u30E9\u30A4\u30D6\u306E\u30EB\u30FC\u30C8\u3084 Windows \u30D5\u30A9\u30EB\u30C0\u30FC\u306F\u4F7F\u7528\u3067\u304D\u307E\u305B\u3093\u3002"
        InstallFolderNotEmpty = ConvertFrom-PatchyUnicodeEscapes "\u30D5\u30A9\u30EB\u30C0\u30FC {0} \u306B\u306F\u65E2\u306B\u4ED6\u306E\u30D5\u30A1\u30A4\u30EB\u304C\u3042\u308A\u307E\u3059\u3002Patchy \u306E\u30A2\u30F3\u30A4\u30F3\u30B9\u30C8\u30FC\u30EB\u3067 Patchy \u4EE5\u5916\u306E\u30D5\u30A1\u30A4\u30EB\u304C\u524A\u9664\u3055\u308C\u306A\u3044\u3088\u3046\u3001\u7A7A\u306E\u30D5\u30A9\u30EB\u30C0\u30FC\u304B\u65B0\u3057\u3044\u30D5\u30A9\u30EB\u30C0\u30FC\u540D\u3092\u6307\u5B9A\u3057\u3066\u304F\u3060\u3055\u3044\u3002"
        InstallFolderNotWritable = ConvertFrom-PatchyUnicodeEscapes "{0} \u306B\u66F8\u304D\u8FBC\u3081\u307E\u305B\u3093\u3002\u3053\u306E\u30A4\u30F3\u30B9\u30C8\u30FC\u30E9\u30FC\u306F\u7BA1\u7406\u8005\u6A29\u9650\u3092\u4F7F\u7528\u3057\u306A\u3044\u305F\u3081\u3001\u30E6\u30FC\u30B6\u30FC \u30D5\u30A9\u30EB\u30C0\u30FC\u5185\u3084\u5225\u306E\u30C9\u30E9\u30A4\u30D6\u306A\u3069\u3001\u66F8\u304D\u8FBC\u307F\u53EF\u80FD\u306A\u30D5\u30A9\u30EB\u30C0\u30FC\u3092\u9078\u629E\u3057\u3066\u304F\u3060\u3055\u3044\u3002"
    }
    de = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy wird derzeit ausgef\u00FChrt. Speichern Sie Ihre Arbeit, schlie\u00DFen Sie Patchy und klicken Sie dann auf \u201EWiederholen\u201C, um die Installation fortzusetzen."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy wird derzeit ausgef\u00FChrt. Schlie\u00DFen Sie Patchy und f\u00FChren Sie das Setup erneut aus."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy konnte nicht aktualisiert werden, weil installierte Dateien gerade verwendet werden. Schlie\u00DFen Sie Patchy und f\u00FChren Sie das Setup erneut aus."
        LockedFileRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Setup kann {0} nicht ersetzen, weil die Datei von einem anderen Programm verwendet wird. Schlie\u00DFen Sie dieses Programm und klicken Sie dann auf \u201EWiederholen\u201C, um fortzufahren."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "Installation abgebrochen."
        InstallFolderInvalid = ConvertFrom-PatchyUnicodeEscapes "W\u00E4hlen Sie einen vollst\u00E4ndigen Ordnerpfad f\u00FCr Patchy, zum Beispiel D:\Apps\Patchy. Ein Laufwerksstamm oder der Windows-Ordner kann nicht verwendet werden."
        InstallFolderNotEmpty = ConvertFrom-PatchyUnicodeEscapes "Der Ordner {0} enth\u00E4lt bereits andere Dateien. W\u00E4hlen Sie einen leeren Ordner oder einen neuen Ordnernamen, damit beim Deinstallieren von Patchy nur dessen eigene Dateien entfernt werden."
        InstallFolderNotWritable = ConvertFrom-PatchyUnicodeEscapes "Setup kann nicht in {0} schreiben. Dieses Installationsprogramm verwendet keine Administratorrechte. W\u00E4hlen Sie daher einen Ordner, in den Sie schreiben k\u00F6nnen, zum Beispiel in Ihrem Benutzerordner oder auf einem anderen Laufwerk."
    }
    es = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy se est\u00E1 ejecutando. Guarde su trabajo, cierre Patchy y haga clic en Reintentar para continuar con la instalaci\u00F3n."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy se est\u00E1 ejecutando. Cierre Patchy y vuelva a ejecutar el instalador."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "No se pudo actualizar Patchy porque los archivos instalados est\u00E1n en uso. Cierre Patchy y vuelva a ejecutar el instalador."
        LockedFileRetryMessage = ConvertFrom-PatchyUnicodeEscapes "El instalador no puede reemplazar {0} porque otro programa lo est\u00E1 usando. Cierre ese programa y haga clic en Reintentar para continuar."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "Instalaci\u00F3n cancelada."
        InstallFolderInvalid = ConvertFrom-PatchyUnicodeEscapes "Elija una ruta de carpeta completa para Patchy, como D:\Apps\Patchy. No se puede usar la ra\u00EDz de una unidad ni la carpeta de Windows."
        InstallFolderNotEmpty = ConvertFrom-PatchyUnicodeEscapes "La carpeta {0} ya contiene otros archivos. Elija una carpeta vac\u00EDa o un nombre de carpeta nuevo, para que al desinstalar Patchy nunca se eliminen archivos que no sean suyos."
        InstallFolderNotWritable = ConvertFrom-PatchyUnicodeEscapes "El instalador no puede escribir en {0}. Este instalador no usa permisos de administrador, as\u00ED que elija una carpeta en la que pueda escribir, como una dentro de su carpeta de usuario o en otra unidad."
    }
    fr = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy est en cours d'ex\u00E9cution. Enregistrez votre travail, fermez Patchy, puis cliquez sur R\u00E9essayer pour poursuivre l'installation."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy est en cours d'ex\u00E9cution. Fermez Patchy, puis relancez le programme d'installation."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Impossible de mettre \u00E0 jour Patchy, car des fichiers install\u00E9s sont en cours d'utilisation. Fermez Patchy, puis relancez le programme d'installation."
        LockedFileRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Le programme d'installation ne peut pas remplacer {0}, car un autre programme l'utilise. Fermez ce programme, puis cliquez sur R\u00E9essayer pour continuer."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "Installation annul\u00E9e."
        InstallFolderInvalid = ConvertFrom-PatchyUnicodeEscapes "Choisissez un chemin de dossier complet pour Patchy, par exemple D:\Apps\Patchy. La racine d'un lecteur ou le dossier Windows ne peuvent pas \u00EAtre utilis\u00E9s."
        InstallFolderNotEmpty = ConvertFrom-PatchyUnicodeEscapes "Le dossier {0} contient d\u00E9j\u00E0 d'autres fichiers. Choisissez un dossier vide ou un nouveau nom de dossier, afin que la d\u00E9sinstallation de Patchy ne supprime jamais de fichiers qui ne lui appartiennent pas."
        InstallFolderNotWritable = ConvertFrom-PatchyUnicodeEscapes "Le programme d'installation ne peut pas \u00E9crire dans {0}. Il n'utilise pas les droits d'administrateur : choisissez un dossier dans lequel vous pouvez \u00E9crire, par exemple dans votre dossier utilisateur ou sur un autre lecteur."
    }
    it = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u00E8 in esecuzione. Salva il lavoro, chiudi Patchy e fai clic su Riprova per continuare l'installazione."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u00E8 in esecuzione. Chiudi Patchy ed esegui di nuovo il programma di installazione."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Impossibile aggiornare Patchy perch\u00E9 i file installati sono in uso. Chiudi Patchy ed esegui di nuovo il programma di installazione."
        LockedFileRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Il programma di installazione non pu\u00F2 sostituire {0} perch\u00E9 un altro programma lo sta usando. Chiudi quel programma e fai clic su Riprova per continuare."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "Installazione annullata."
        InstallFolderInvalid = ConvertFrom-PatchyUnicodeEscapes "Scegli un percorso di cartella completo per Patchy, ad esempio D:\Apps\Patchy. Non \u00E8 possibile usare la radice di un'unit\u00E0 o la cartella di Windows."
        InstallFolderNotEmpty = ConvertFrom-PatchyUnicodeEscapes "La cartella {0} contiene gi\u00E0 altri file. Scegli una cartella vuota o un nuovo nome di cartella, cos\u00EC la disinstallazione di Patchy non rimuover\u00E0 mai file che non gli appartengono."
        InstallFolderNotWritable = ConvertFrom-PatchyUnicodeEscapes "Il programma di installazione non pu\u00F2 scrivere in {0}. Non usa i diritti di amministratore, quindi scegli una cartella in cui puoi scrivere, ad esempio nella tua cartella utente o su un'altra unit\u00E0."
    }
    ko = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy\uAC00 \uC2E4\uD589 \uC911\uC785\uB2C8\uB2E4. \uC791\uC5C5\uC744 \uC800\uC7A5\uD558\uACE0 Patchy\uB97C \uB2EB\uC740 \uD6C4 \uB2E4\uC2DC \uC2DC\uB3C4\uB97C \uD074\uB9AD\uD558\uC5EC \uC124\uCE58\uB97C \uACC4\uC18D\uD558\uC138\uC694."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy\uAC00 \uC2E4\uD589 \uC911\uC785\uB2C8\uB2E4. Patchy\uB97C \uB2EB\uACE0 \uC124\uCE58 \uD504\uB85C\uADF8\uB7A8\uC744 \uB2E4\uC2DC \uC2E4\uD589\uD558\uC138\uC694."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "\uC124\uCE58\uB41C \uD30C\uC77C\uC774 \uC0AC\uC6A9 \uC911\uC774\uC5B4\uC11C Patchy\uB97C \uC5C5\uB370\uC774\uD2B8\uD560 \uC218 \uC5C6\uC2B5\uB2C8\uB2E4. Patchy\uB97C \uB2EB\uACE0 \uC124\uCE58 \uD504\uB85C\uADF8\uB7A8\uC744 \uB2E4\uC2DC \uC2E4\uD589\uD558\uC138\uC694."
        LockedFileRetryMessage = ConvertFrom-PatchyUnicodeEscapes "\uB2E4\uB978 \uD504\uB85C\uADF8\uB7A8\uC5D0\uC11C {0}\uC744(\uB97C) \uC0AC\uC6A9 \uC911\uC774\uC5B4\uC11C \uBC14\uAFC0 \uC218 \uC5C6\uC2B5\uB2C8\uB2E4. \uD574\uB2F9 \uD504\uB85C\uADF8\uB7A8\uC744 \uB2EB\uC740 \uD6C4 \uB2E4\uC2DC \uC2DC\uB3C4\uB97C \uD074\uB9AD\uD558\uC5EC \uACC4\uC18D\uD558\uC138\uC694."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "\uC124\uCE58\uAC00 \uCDE8\uC18C\uB418\uC5C8\uC2B5\uB2C8\uB2E4."
        InstallFolderInvalid = ConvertFrom-PatchyUnicodeEscapes "Patchy\uB97C \uC124\uCE58\uD560 \uC804\uCCB4 \uD3F4\uB354 \uACBD\uB85C\uB97C \uC9C0\uC815\uD558\uC138\uC694(\uC608: D:\Apps\Patchy). \uB4DC\uB77C\uC774\uBE0C \uB8E8\uD2B8\uB098 Windows \uD3F4\uB354\uB294 \uC0AC\uC6A9\uD560 \uC218 \uC5C6\uC2B5\uB2C8\uB2E4."
        InstallFolderNotEmpty = ConvertFrom-PatchyUnicodeEscapes "{0} \uD3F4\uB354\uC5D0 \uC774\uBBF8 \uB2E4\uB978 \uD30C\uC77C\uC774 \uC788\uC2B5\uB2C8\uB2E4. Patchy\uB97C \uC81C\uAC70\uD560 \uB54C Patchy\uC758 \uD30C\uC77C\uC774 \uC544\uB2CC \uD56D\uBAA9\uC774 \uC0AD\uC81C\uB418\uC9C0 \uC54A\uB3C4\uB85D \uBE48 \uD3F4\uB354\uB098 \uC0C8 \uD3F4\uB354 \uC774\uB984\uC744 \uC120\uD0DD\uD558\uC138\uC694."
        InstallFolderNotWritable = ConvertFrom-PatchyUnicodeEscapes "{0}\uC5D0 \uC4F8 \uC218 \uC5C6\uC2B5\uB2C8\uB2E4. \uC774 \uC124\uCE58 \uD504\uB85C\uADF8\uB7A8\uC740 \uAD00\uB9AC\uC790 \uAD8C\uD55C\uC744 \uC0AC\uC6A9\uD558\uC9C0 \uC54A\uC73C\uBBC0\uB85C \uC0AC\uC6A9\uC790 \uD3F4\uB354\uB098 \uB2E4\uB978 \uB4DC\uB77C\uC774\uBE0C\uCC98\uB7FC \uC4F0\uAE30 \uAD8C\uD55C\uC774 \uC788\uB294 \uD3F4\uB354\uB97C \uC120\uD0DD\uD558\uC138\uC694."
    }
    pl = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy jest uruchomiony. Zapisz swoj\u0105 prac\u0119, zamknij Patchy, a nast\u0119pnie kliknij Pon\u00F3w pr\u00F3b\u0119, aby kontynuowa\u0107 instalacj\u0119."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy jest uruchomiony. Zamknij Patchy i ponownie uruchom instalator."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Nie mo\u017Cna zaktualizowa\u0107 Patchy, poniewa\u017C zainstalowane pliki s\u0105 u\u017Cywane. Zamknij Patchy i ponownie uruchom instalator."
        LockedFileRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Instalator nie mo\u017Ce zast\u0105pi\u0107 pliku {0}, poniewa\u017C u\u017Cywa go inny program. Zamknij ten program, a nast\u0119pnie kliknij Pon\u00F3w pr\u00F3b\u0119, aby kontynuowa\u0107."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "Instalacja zosta\u0142a anulowana."
        InstallFolderInvalid = ConvertFrom-PatchyUnicodeEscapes "Wybierz pe\u0142n\u0105 \u015Bcie\u017Ck\u0119 folderu dla Patchy, na przyk\u0142ad D:\Apps\Patchy. Nie mo\u017Cna u\u017Cy\u0107 katalogu g\u0142\u00F3wnego dysku ani folderu Windows."
        InstallFolderNotEmpty = ConvertFrom-PatchyUnicodeEscapes "Folder {0} zawiera ju\u017C inne pliki. Wybierz pusty folder lub now\u0105 nazw\u0119 folderu, aby odinstalowanie Patchy nigdy nie usun\u0119\u0142o plik\u00F3w, kt\u00F3re do niego nie nale\u017C\u0105."
        InstallFolderNotWritable = ConvertFrom-PatchyUnicodeEscapes "Instalator nie mo\u017Ce zapisywa\u0107 w {0}. Ten instalator nie u\u017Cywa uprawnie\u0144 administratora, wi\u0119c wybierz folder, w kt\u00F3rym mo\u017Cesz zapisywa\u0107, na przyk\u0142ad w folderze u\u017Cytkownika lub na innym dysku."
    }
    pt_BR = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "O Patchy est\u00E1 em execu\u00E7\u00E3o. Salve seu trabalho, feche o Patchy e clique em Repetir para continuar a instala\u00E7\u00E3o."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "O Patchy est\u00E1 em execu\u00E7\u00E3o. Feche o Patchy e execute o instalador novamente."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "N\u00E3o foi poss\u00EDvel atualizar o Patchy porque os arquivos instalados est\u00E3o em uso. Feche o Patchy e execute o instalador novamente."
        LockedFileRetryMessage = ConvertFrom-PatchyUnicodeEscapes "O instalador n\u00E3o pode substituir {0} porque outro programa o est\u00E1 usando. Feche esse programa e clique em Repetir para continuar."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "Instala\u00E7\u00E3o cancelada."
        InstallFolderInvalid = ConvertFrom-PatchyUnicodeEscapes "Escolha um caminho de pasta completo para o Patchy, como D:\Apps\Patchy. N\u00E3o \u00E9 poss\u00EDvel usar a raiz de uma unidade nem a pasta do Windows."
        InstallFolderNotEmpty = ConvertFrom-PatchyUnicodeEscapes "A pasta {0} j\u00E1 cont\u00E9m outros arquivos. Escolha uma pasta vazia ou um novo nome de pasta, para que a desinstala\u00E7\u00E3o do Patchy nunca remova arquivos que n\u00E3o sejam dele."
        InstallFolderNotWritable = ConvertFrom-PatchyUnicodeEscapes "O instalador n\u00E3o pode gravar em {0}. Este instalador n\u00E3o usa direitos de administrador, ent\u00E3o escolha uma pasta na qual voc\u00EA possa gravar, como uma dentro da sua pasta de usu\u00E1rio ou em outra unidade."
    }
    ru = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u0443\u0436\u0435 \u0437\u0430\u043F\u0443\u0449\u0435\u043D. \u0421\u043E\u0445\u0440\u0430\u043D\u0438\u0442\u0435 \u0440\u0430\u0431\u043E\u0442\u0443, \u0437\u0430\u043A\u0440\u043E\u0439\u0442\u0435 Patchy \u0438 \u043D\u0430\u0436\u043C\u0438\u0442\u0435 \u00AB\u041F\u043E\u0432\u0442\u043E\u0440\u0438\u0442\u044C\u00BB, \u0447\u0442\u043E\u0431\u044B \u043F\u0440\u043E\u0434\u043E\u043B\u0436\u0438\u0442\u044C \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0443."
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u0443\u0436\u0435 \u0437\u0430\u043F\u0443\u0449\u0435\u043D. \u0417\u0430\u043A\u0440\u043E\u0439\u0442\u0435 Patchy \u0438 \u0437\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u0435 \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0443 \u0441\u043D\u043E\u0432\u0430."
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "\u041D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C \u043E\u0431\u043D\u043E\u0432\u0438\u0442\u044C Patchy, \u043F\u043E\u0441\u043A\u043E\u043B\u044C\u043A\u0443 \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u043B\u0435\u043D\u043D\u044B\u0435 \u0444\u0430\u0439\u043B\u044B \u0438\u0441\u043F\u043E\u043B\u044C\u0437\u0443\u044E\u0442\u0441\u044F. \u0417\u0430\u043A\u0440\u043E\u0439\u0442\u0435 Patchy \u0438 \u0437\u0430\u043F\u0443\u0441\u0442\u0438\u0442\u0435 \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0443 \u0441\u043D\u043E\u0432\u0430."
        LockedFileRetryMessage = ConvertFrom-PatchyUnicodeEscapes "\u041F\u0440\u043E\u0433\u0440\u0430\u043C\u043C\u0430 \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0438 \u043D\u0435 \u043C\u043E\u0436\u0435\u0442 \u0437\u0430\u043C\u0435\u043D\u0438\u0442\u044C {0}, \u0442\u0430\u043A \u043A\u0430\u043A \u0444\u0430\u0439\u043B \u0438\u0441\u043F\u043E\u043B\u044C\u0437\u0443\u0435\u0442\u0441\u044F \u0434\u0440\u0443\u0433\u043E\u0439 \u043F\u0440\u043E\u0433\u0440\u0430\u043C\u043C\u043E\u0439. \u0417\u0430\u043A\u0440\u043E\u0439\u0442\u0435 \u044D\u0442\u0443 \u043F\u0440\u043E\u0433\u0440\u0430\u043C\u043C\u0443 \u0438 \u043D\u0430\u0436\u043C\u0438\u0442\u0435 \u00AB\u041F\u043E\u0432\u0442\u043E\u0440\u0438\u0442\u044C\u00BB, \u0447\u0442\u043E\u0431\u044B \u043F\u0440\u043E\u0434\u043E\u043B\u0436\u0438\u0442\u044C."
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "\u0423\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0430 \u043E\u0442\u043C\u0435\u043D\u0435\u043D\u0430."
        InstallFolderInvalid = ConvertFrom-PatchyUnicodeEscapes "\u0423\u043A\u0430\u0436\u0438\u0442\u0435 \u043F\u043E\u043B\u043D\u044B\u0439 \u043F\u0443\u0442\u044C \u043A \u043F\u0430\u043F\u043A\u0435 \u0434\u043B\u044F Patchy, \u043D\u0430\u043F\u0440\u0438\u043C\u0435\u0440 D:\Apps\Patchy. \u041A\u043E\u0440\u0435\u043D\u044C \u0434\u0438\u0441\u043A\u0430 \u0438 \u043F\u0430\u043F\u043A\u0443 Windows \u0438\u0441\u043F\u043E\u043B\u044C\u0437\u043E\u0432\u0430\u0442\u044C \u043D\u0435\u043B\u044C\u0437\u044F."
        InstallFolderNotEmpty = ConvertFrom-PatchyUnicodeEscapes "\u041F\u0430\u043F\u043A\u0430 {0} \u0443\u0436\u0435 \u0441\u043E\u0434\u0435\u0440\u0436\u0438\u0442 \u0434\u0440\u0443\u0433\u0438\u0435 \u0444\u0430\u0439\u043B\u044B. \u0412\u044B\u0431\u0435\u0440\u0438\u0442\u0435 \u043F\u0443\u0441\u0442\u0443\u044E \u043F\u0430\u043F\u043A\u0443 \u0438\u043B\u0438 \u043D\u043E\u0432\u043E\u0435 \u0438\u043C\u044F \u043F\u0430\u043F\u043A\u0438, \u0447\u0442\u043E\u0431\u044B \u043F\u0440\u0438 \u0443\u0434\u0430\u043B\u0435\u043D\u0438\u0438 Patchy \u043D\u0435 \u0443\u0434\u0430\u043B\u044F\u043B\u0438\u0441\u044C \u0447\u0443\u0436\u0438\u0435 \u0444\u0430\u0439\u043B\u044B."
        InstallFolderNotWritable = ConvertFrom-PatchyUnicodeEscapes "\u041F\u0440\u043E\u0433\u0440\u0430\u043C\u043C\u0430 \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0438 \u043D\u0435 \u043C\u043E\u0436\u0435\u0442 \u0437\u0430\u043F\u0438\u0441\u044B\u0432\u0430\u0442\u044C \u0432 {0}. \u041E\u043D\u0430 \u043D\u0435 \u0438\u0441\u043F\u043E\u043B\u044C\u0437\u0443\u0435\u0442 \u043F\u0440\u0430\u0432\u0430 \u0430\u0434\u043C\u0438\u043D\u0438\u0441\u0442\u0440\u0430\u0442\u043E\u0440\u0430, \u043F\u043E\u044D\u0442\u043E\u043C\u0443 \u0432\u044B\u0431\u0435\u0440\u0438\u0442\u0435 \u043F\u0430\u043F\u043A\u0443, \u0432 \u043A\u043E\u0442\u043E\u0440\u0443\u044E \u0443 \u0432\u0430\u0441 \u0435\u0441\u0442\u044C \u043F\u0440\u0430\u0432\u043E \u0437\u0430\u043F\u0438\u0441\u0438, \u043D\u0430\u043F\u0440\u0438\u043C\u0435\u0440 \u0432 \u043F\u0430\u043F\u043A\u0435 \u043F\u043E\u043B\u044C\u0437\u043E\u0432\u0430\u0442\u0435\u043B\u044F \u0438\u043B\u0438 \u043D\u0430 \u0434\u0440\u0443\u0433\u043E\u043C \u0434\u0438\u0441\u043A\u0435."
    }
    zh_CN = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u6B63\u5728\u8FD0\u884C\u3002\u8BF7\u4FDD\u5B58\u60A8\u7684\u5DE5\u4F5C\uFF0C\u5173\u95ED Patchy\uFF0C\u7136\u540E\u5355\u51FB\u201C\u91CD\u8BD5\u201D\u7EE7\u7EED\u5B89\u88C5\u3002"
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u6B63\u5728\u8FD0\u884C\u3002\u8BF7\u5173\u95ED Patchy\uFF0C\u7136\u540E\u91CD\u65B0\u8FD0\u884C\u5B89\u88C5\u7A0B\u5E8F\u3002"
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "\u65E0\u6CD5\u66F4\u65B0 Patchy\uFF0C\u56E0\u4E3A\u5DF2\u5B89\u88C5\u7684\u6587\u4EF6\u6B63\u5728\u4F7F\u7528\u4E2D\u3002\u8BF7\u5173\u95ED Patchy\uFF0C\u7136\u540E\u91CD\u65B0\u8FD0\u884C\u5B89\u88C5\u7A0B\u5E8F\u3002"
        LockedFileRetryMessage = ConvertFrom-PatchyUnicodeEscapes "\u5B89\u88C5\u7A0B\u5E8F\u65E0\u6CD5\u66FF\u6362 {0}\uFF0C\u56E0\u4E3A\u5176\u4ED6\u7A0B\u5E8F\u6B63\u5728\u4F7F\u7528\u5B83\u3002\u8BF7\u5173\u95ED\u8BE5\u7A0B\u5E8F\uFF0C\u7136\u540E\u5355\u51FB\u201C\u91CD\u8BD5\u201D\u7EE7\u7EED\u3002"
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "\u5B89\u88C5\u5DF2\u53D6\u6D88\u3002"
        InstallFolderInvalid = ConvertFrom-PatchyUnicodeEscapes "\u8BF7\u4E3A Patchy \u9009\u62E9\u5B8C\u6574\u7684\u6587\u4EF6\u5939\u8DEF\u5F84\uFF0C\u4F8B\u5982 D:\Apps\Patchy\u3002\u4E0D\u80FD\u4F7F\u7528\u9A71\u52A8\u5668\u6839\u76EE\u5F55\u6216 Windows \u6587\u4EF6\u5939\u3002"
        InstallFolderNotEmpty = ConvertFrom-PatchyUnicodeEscapes "\u6587\u4EF6\u5939 {0} \u4E2D\u5DF2\u6709\u5176\u4ED6\u6587\u4EF6\u3002\u8BF7\u9009\u62E9\u7A7A\u6587\u4EF6\u5939\u6216\u65B0\u7684\u6587\u4EF6\u5939\u540D\u79F0\uFF0C\u4EE5\u786E\u4FDD\u5378\u8F7D Patchy \u65F6\u4E0D\u4F1A\u5220\u9664\u4E0D\u5C5E\u4E8E\u5B83\u7684\u6587\u4EF6\u3002"
        InstallFolderNotWritable = ConvertFrom-PatchyUnicodeEscapes "\u5B89\u88C5\u7A0B\u5E8F\u65E0\u6CD5\u5199\u5165 {0}\u3002\u6B64\u5B89\u88C5\u7A0B\u5E8F\u4E0D\u4F7F\u7528\u7BA1\u7406\u5458\u6743\u9650\uFF0C\u8BF7\u9009\u62E9\u60A8\u6709\u5199\u5165\u6743\u9650\u7684\u6587\u4EF6\u5939\uFF0C\u4F8B\u5982\u7528\u6237\u6587\u4EF6\u5939\u4E2D\u7684\u6587\u4EF6\u5939\u6216\u5176\u4ED6\u9A71\u52A8\u5668\u3002"
    }
    zh_TW = @{
        RunningPatchyRetryMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u6B63\u5728\u57F7\u884C\u4E2D\u3002\u8ACB\u5132\u5B58\u60A8\u7684\u5DE5\u4F5C\uFF0C\u95DC\u9589 Patchy\uFF0C\u7136\u5F8C\u6309\u4E00\u4E0B\u300C\u91CD\u8A66\u300D\u4EE5\u7E7C\u7E8C\u5B89\u88DD\u3002"
        RunningPatchyQuietMessage = ConvertFrom-PatchyUnicodeEscapes "Patchy \u6B63\u5728\u57F7\u884C\u4E2D\u3002\u8ACB\u95DC\u9589 Patchy\uFF0C\u7136\u5F8C\u91CD\u65B0\u57F7\u884C\u5B89\u88DD\u7A0B\u5F0F\u3002"
        FileInUseQuietMessage = ConvertFrom-PatchyUnicodeEscapes "\u7121\u6CD5\u66F4\u65B0 Patchy\uFF0C\u56E0\u70BA\u5DF2\u5B89\u88DD\u7684\u6A94\u6848\u6B63\u5728\u4F7F\u7528\u4E2D\u3002\u8ACB\u95DC\u9589 Patchy\uFF0C\u7136\u5F8C\u91CD\u65B0\u57F7\u884C\u5B89\u88DD\u7A0B\u5F0F\u3002"
        LockedFileRetryMessage = ConvertFrom-PatchyUnicodeEscapes "\u5B89\u88DD\u7A0B\u5F0F\u7121\u6CD5\u53D6\u4EE3 {0}\uFF0C\u56E0\u70BA\u5176\u4ED6\u7A0B\u5F0F\u6B63\u5728\u4F7F\u7528\u5B83\u3002\u8ACB\u95DC\u9589\u8A72\u7A0B\u5F0F\uFF0C\u7136\u5F8C\u6309\u4E00\u4E0B\u300C\u91CD\u8A66\u300D\u4EE5\u7E7C\u7E8C\u3002"
        InstallationCanceled = ConvertFrom-PatchyUnicodeEscapes "\u5B89\u88DD\u5DF2\u53D6\u6D88\u3002"
        InstallFolderInvalid = ConvertFrom-PatchyUnicodeEscapes "\u8ACB\u70BA Patchy \u9078\u64C7\u5B8C\u6574\u7684\u8CC7\u6599\u593E\u8DEF\u5F91\uFF0C\u4F8B\u5982 D:\Apps\Patchy\u3002\u4E0D\u80FD\u4F7F\u7528\u78C1\u789F\u6A5F\u6839\u76EE\u9304\u6216 Windows \u8CC7\u6599\u593E\u3002"
        InstallFolderNotEmpty = ConvertFrom-PatchyUnicodeEscapes "\u8CC7\u6599\u593E {0} \u4E2D\u5DF2\u6709\u5176\u4ED6\u6A94\u6848\u3002\u8ACB\u9078\u64C7\u7A7A\u7684\u8CC7\u6599\u593E\u6216\u65B0\u7684\u8CC7\u6599\u593E\u540D\u7A31\uFF0C\u4EE5\u78BA\u4FDD\u89E3\u9664\u5B89\u88DD Patchy \u6642\u4E0D\u6703\u522A\u9664\u4E0D\u5C6C\u65BC\u5B83\u7684\u6A94\u6848\u3002"
        InstallFolderNotWritable = ConvertFrom-PatchyUnicodeEscapes "\u5B89\u88DD\u7A0B\u5F0F\u7121\u6CD5\u5BEB\u5165 {0}\u3002\u6B64\u5B89\u88DD\u7A0B\u5F0F\u4E0D\u4F7F\u7528\u7CFB\u7D71\u7BA1\u7406\u54E1\u6B0A\u9650\uFF0C\u8ACB\u9078\u64C7\u60A8\u6709\u5BEB\u5165\u6B0A\u9650\u7684\u8CC7\u6599\u593E\uFF0C\u4F8B\u5982\u4F7F\u7528\u8005\u8CC7\u6599\u593E\u4E2D\u7684\u8CC7\u6599\u593E\u6216\u5176\u4ED6\u78C1\u789F\u6A5F\u3002"
    }
}

function Get-PatchyInstallerLanguage {
    # Maps the Windows UI culture to one of the shipped installer languages; English is the fallback.
    $culture = [Globalization.CultureInfo]::CurrentUICulture.Name
    if ([string]::IsNullOrWhiteSpace($culture)) {
        return "en"
    }

    $tags = $culture -split "-"
    $language = $tags[0].ToLowerInvariant()
    if ($language -eq "zh") {
        # Traditional Chinese for Taiwan, Hong Kong, Macao, and any Hant script tag (zh-TW, zh-Hant-HK, ...).
        # Every other Chinese culture (zh-CN, zh-Hans-CN, zh-SG, ...) reads Simplified.
        $traditionalTags = @("tw", "hk", "mo", "hant")
        for ($i = 1; $i -lt $tags.Length; $i++) {
            if ($traditionalTags -contains $tags[$i].ToLowerInvariant()) {
                return "zh_TW"
            }
        }
        return "zh_CN"
    }

    if ($language -eq "pt") {
        return "pt_BR"
    }
    if (@("de", "es", "fr", "it", "ja", "ko", "pl", "ru") -contains $language) {
        return $language
    }
    return "en"
}

function Get-PatchyInstallerText {
    param([Parameter(Mandatory = $true)][string]$Key)

    $language = Get-PatchyInstallerLanguage
    if ($PatchyInstallerText.ContainsKey($language) -and $PatchyInstallerText[$language].ContainsKey($Key)) {
        return $PatchyInstallerText[$language][$Key]
    }
    return $PatchyInstallerText["en"][$Key]
}

function Test-PathAtOrInsideRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $trimChars = [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd($trimChars)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd($trimChars)
    return $fullPath.Equals($fullRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $fullPath.StartsWith($fullRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}

function Get-RunningInstalledPatchyProcess {
    param([Parameter(Mandatory = $true)][string]$InstallRoot)

    Get-Process -Name "patchy" -ErrorAction SilentlyContinue | Where-Object {
        $processPath = $null
        try {
            $processPath = $_.MainModule.FileName
        } catch {
            $processPath = $null
        }
        $processPath -and (Test-PathAtOrInsideRoot -Path $processPath -Root $InstallRoot)
    }
}

function Request-ClosePatchyRetry {
    param(
        [object]$Owner = $null,

        [string]$Message = ""
    )

    Add-Type -AssemblyName System.Windows.Forms
    $message = $Message
    if (-not $message) {
        $message = Get-PatchyInstallerText "RunningPatchyRetryMessage"
    }
    if ($Owner -ne $null) {
        $result = [System.Windows.Forms.MessageBox]::Show(
            $Owner,
            $message,
            "Patchy Setup",
            [System.Windows.Forms.MessageBoxButtons]::RetryCancel,
            [System.Windows.Forms.MessageBoxIcon]::Warning,
            [System.Windows.Forms.MessageBoxDefaultButton]::Button1
        )
    } else {
        $result = [System.Windows.Forms.MessageBox]::Show(
            $message,
            "Patchy Setup",
            [System.Windows.Forms.MessageBoxButtons]::RetryCancel,
            [System.Windows.Forms.MessageBoxIcon]::Warning,
            [System.Windows.Forms.MessageBoxDefaultButton]::Button1
        )
    }
    return $result -eq [System.Windows.Forms.DialogResult]::Retry
}

function Confirm-PatchyClosedForInstall {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [bool]$Quiet = $false,

        [object]$Owner = $null
    )

    while (@(Get-RunningInstalledPatchyProcess -InstallRoot $InstallRoot).Count -gt 0) {
        if ($Quiet -or -not [Environment]::UserInteractive) {
            throw (Get-PatchyInstallerText "RunningPatchyQuietMessage")
        }
        if (-not (Request-ClosePatchyRetry -Owner $Owner)) {
            throw (New-Object System.OperationCanceledException (Get-PatchyInstallerText "InstallationCanceled"))
        }
    }
}

# Update mode: the Patchy that asked for the update is still closing (it can take several
# seconds to finish its background work). True once the process is gone, false when it is
# still there after the timeout; the running-Patchy check then asks the user.
function Wait-PatchyProcessExit {
    param(
        [Parameter(Mandatory = $true)]
        [int]$ProcessId,

        [int]$TimeoutSeconds = 120
    )

    $process = $null
    try {
        $process = [System.Diagnostics.Process]::GetProcessById($ProcessId)
    } catch {
        return $true
    }
    try {
        return $process.WaitForExit($TimeoutSeconds * 1000)
    } catch {
        return $true
    } finally {
        $process.Dispose()
    }
}

# Update mode: the Patchy that asked for the update is still there after the wait, but has
# no window left. Patchy 1.13 and earlier accepted an update while a file from the command
# line was still opening, before their event loop had started: the window closed, this
# installer was started, and the process then sat in an event loop with no window (Qt's
# quit on the last window closed is inactive before exec). Nobody could close it from the
# desktop, and this installer asked the user to close a Patchy they could not see. Every
# document was settled before that window closed, so ending it loses nothing. True when the
# process id is alive, runs from inside the install folder and owns no visible top-level
# window. A Patchy with a window (the user cancelled a save prompt) is never touched, nor is
# any process outside the install folder (a connector, another install).
function Test-PatchyHeadlessUpdateRequester {
    param(
        [Parameter(Mandatory = $true)]
        [int]$ProcessId,

        [Parameter(Mandatory = $true)]
        [string]$InstallRoot
    )

    if ($ProcessId -le 0) {
        return $false
    }
    try {
        $process = Get-Process -Id $ProcessId -ErrorAction Stop
    } catch {
        return $false
    }
    $executable = $null
    try {
        $executable = $process.Path
    } catch {
    }
    if (-not $executable) {
        try {
            $executable = (Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId" -OperationTimeoutSec 5 -ErrorAction Stop).ExecutablePath
        } catch {
        }
    }
    if (-not $executable -or -not (Test-PathAtOrInsideRoot -Path $executable -Root $InstallRoot)) {
        return $false
    }
    if (-not ("PatchyProcessWindows" -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class PatchyProcessWindows {
    private delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hwnd);
    // Visible top-level windows of the process, owned dialogs included (Process.MainWindowHandle
    // skips owned windows, so a save prompt over a hidden main window would count as none).
    public static int CountVisibleTopLevelWindows(int processId) {
        int count = 0;
        EnumWindows((hwnd, lParam) => {
            uint owner;
            GetWindowThreadProcessId(hwnd, out owner);
            if (owner == (uint)processId && IsWindowVisible(hwnd)) { count++; }
            return true;
        }, IntPtr.Zero);
        return count;
    }
}
"@
    }
    return ([PatchyProcessWindows]::CountVisibleTopLevelWindows($ProcessId) -eq 0)
}

# Ends the headless requester (see above) by its exact process id and waits for it to go.
# True once it is gone; false when the process is not a headless requester or would not end.
function Stop-PatchyHeadlessUpdateRequester {
    param(
        [Parameter(Mandatory = $true)]
        [int]$ProcessId,

        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [int]$TimeoutSeconds = 15
    )

    if (-not (Test-PatchyHeadlessUpdateRequester -ProcessId $ProcessId -InstallRoot $InstallRoot)) {
        return $false
    }
    try {
        Stop-Process -Id $ProcessId -Force -ErrorAction Stop
    } catch {
        return (Wait-PatchyProcessExit -ProcessId $ProcessId -TimeoutSeconds 0)
    }
    $gone = Wait-PatchyProcessExit -ProcessId $ProcessId -TimeoutSeconds $TimeoutSeconds
    if ($gone) {
        Write-Host "Ended Patchy process $ProcessId, which had no window left after asking for the update."
    }
    return $gone
}

function Test-PatchyFileInUseException {
    param($Exception)

    while ($Exception -ne $null) {
        if ($Exception -is [System.IO.IOException]) {
            $win32Code = $Exception.HResult -band 0xffff
            if ($win32Code -eq 32 -or $win32Code -eq 33) {
                return $true
            }
        }
        if ($Exception.Message -match "being used by another process") {
            return $true
        }
        $Exception = $Exception.InnerException
    }
    return $false
}

function Test-PatchyFileInUseInstallError {
    param([Parameter(Mandatory = $true)]$ErrorRecord)

    return Test-PatchyFileInUseException -Exception $ErrorRecord.Exception
}

function New-PatchyShortcut {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ShortcutPath,

        [Parameter(Mandatory = $true)]
        [string]$TargetPath,

        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory,

        [string]$IconPath = ""
    )

    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($ShortcutPath)
    $shortcut.TargetPath = $TargetPath
    $shortcut.WorkingDirectory = $WorkingDirectory
    if ($IconPath -and (Test-Path -LiteralPath $IconPath -PathType Leaf)) {
        $shortcut.IconLocation = "$IconPath,0"
    } else {
        $shortcut.IconLocation = "$TargetPath,0"
    }
    $shortcut.Description = "Patchy"
    $shortcut.Save()
}

$ManifestFileName = "PatchyInstallManifest.txt"
$LegacyInstalledRelativePaths = @(
    "patchy.exe",
    "Patchy.ico",
    "UninstallPatchy.exe",
    "UninstallPatchy.ps1",
    "LICENSE",
    "README.md",
    "NOTICE-THIRD-PARTY.md",
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6PrintSupport.dll",
    "Qt6Svg.dll",
    "Qt6Widgets.dll",
    "concrt140.dll",
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll",
    "msvcp140_codecvt_ids.dll",
    "vccorlib140.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "vcruntime140_threads.dll",
    "iconengines\qsvgicon.dll",
    "imageformats\qjpeg.dll",
    "imageformats\qsvg.dll",
    "imageformats\qtiff.dll",
    "imageformats\qwebp.dll",
    "platforms\qwindows.dll",
    "styles\qmodernwindowsstyle.dll",
    "licenses\qt\qtbase-6.8.3.spdx",
    "licenses\qt\qtimageformats-6.8.3.spdx",
    "licenses\qt\qtsvg-6.8.3.spdx"
)

function Test-SafeRelativeInstallPath {
    param([Parameter(Mandatory = $true)][string]$RelativePath)

    if ([IO.Path]::IsPathRooted($RelativePath)) {
        return $false
    }

    $parts = $RelativePath -split '[\\/]'
    foreach ($part in $parts) {
        if ([string]::IsNullOrWhiteSpace($part) -or $part -eq "." -or $part -eq "..") {
            return $false
        }
    }
    return $true
}

function Get-InstalledRelativePaths {
    param([Parameter(Mandatory = $true)][string]$InstallRoot)

    $manifest = Join-Path $InstallRoot $ManifestFileName
    if (Test-Path -LiteralPath $manifest -PathType Leaf) {
        $paths = Get-Content -LiteralPath $manifest | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_) -and (Test-SafeRelativeInstallPath -RelativePath $_)
        }
        if ($paths -notcontains $ManifestFileName) {
            $paths = @($paths) + $ManifestFileName
        }
        return $paths
    }

    return $LegacyInstalledRelativePaths
}

function Remove-EmptyInstallDirectories {
    param([Parameter(Mandatory = $true)][string]$InstallRoot)

    if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) {
        return
    }

    Get-ChildItem -LiteralPath $InstallRoot -Directory -Recurse -Force |
        Sort-Object FullName -Descending |
        ForEach-Object {
            if (-not (Get-ChildItem -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue)) {
                Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue
            }
        }
}

function Remove-PatchyInstalledFiles {
    param([Parameter(Mandatory = $true)][string]$InstallRoot)

    if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) {
        return
    }

    foreach ($relativePath in Get-InstalledRelativePaths -InstallRoot $InstallRoot) {
        if (-not (Test-SafeRelativeInstallPath -RelativePath $relativePath)) {
            continue
        }
        $target = Join-Path $InstallRoot $relativePath
        if (Test-Path -LiteralPath $target -PathType Leaf) {
            Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
        }
    }

    Remove-EmptyInstallDirectories -InstallRoot $InstallRoot
}

# The first installed file another program holds open (a running patchy-mcp.exe connector
# or plug-in host, for example), as a path relative to the install folder, or $null.
function Get-PatchyLockedInstalledFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        # The files the new version will write. An install whose own manifest is missing or
        # damaged still gets every file that is about to be replaced checked.
        [string[]]$ExtraRelativePaths = @()
    )

    if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) {
        return $null
    }

    $candidates = @(Get-InstalledRelativePaths -InstallRoot $InstallRoot) + @($ExtraRelativePaths) | Sort-Object -Unique
    foreach ($relativePath in $candidates) {
        if (-not $relativePath -or -not (Test-SafeRelativeInstallPath -RelativePath $relativePath)) {
            continue
        }
        if ($relativePath -match $SetAsideFilePattern) {
            continue  # an earlier update's leftover; it goes once its program closes
        }
        $target = Join-Path $InstallRoot $relativePath
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            continue
        }
        try {
            $stream = [IO.File]::Open($target, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            $stream.Dispose()
        } catch {
            if (Test-PatchyFileInUseException -Exception $_.Exception) {
                return $relativePath
            }
        }
    }
    return $null
}

# Files of the previous version that a program still had open when Patchy was updated,
# renamed to "<name>.old-<yyyyMMddHHmmss>" (docs/auto-update.md). The suffix keeps Qt from
# loading a set-aside plugin DLL. They stay in the install manifest, so the next install
# and the uninstaller remove them once nothing uses them.
$SetAsideFilePattern = '\.old-\d{14}$'
$script:PatchySetAsideFiles = New-Object System.Collections.Generic.List[string]

# Renames every installed file another program holds open, so the install can put the new
# copy in its place. Windows lets a loaded executable or DLL be renamed, though not replaced
# or deleted; the program using it (an AI app's patchy-mcp.exe connector, typically) keeps
# running the previous version until it restarts. Throws when a file cannot be renamed.
function Move-PatchyLockedInstalledFilesAside {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [string[]]$ExtraRelativePaths = @()
    )

    $stamp = Get-Date -Format "yyyyMMddHHmmss"
    while ($true) {
        $lockedFile = Get-PatchyLockedInstalledFile -InstallRoot $InstallRoot -ExtraRelativePaths $ExtraRelativePaths
        if (-not $lockedFile) {
            return
        }
        $setAside = "$lockedFile.old-$stamp"
        Rename-Item -LiteralPath (Join-Path $InstallRoot $lockedFile) -NewName (Split-Path -Leaf $setAside) -ErrorAction Stop
        $script:PatchySetAsideFiles.Add($setAside)
    }
}

# An install that failed after setting files aside gives them their names back wherever
# the new file never arrived, so the program using them and the old Patchy keep working.
# A set-aside file whose new copy did arrive stays set aside and stays recorded.
function Restore-PatchySetAsideFiles {
    param([Parameter(Mandatory = $true)][string]$InstallRoot)

    foreach ($setAside in @($script:PatchySetAsideFiles)) {
        $original = $setAside -replace $SetAsideFilePattern, ""
        $setAsidePath = Join-Path $InstallRoot $setAside
        $originalPath = Join-Path $InstallRoot $original
        if ((Test-Path -LiteralPath $setAsidePath -PathType Leaf) -and -not (Test-Path -LiteralPath $originalPath)) {
            try {
                Rename-Item -LiteralPath $setAsidePath -NewName (Split-Path -Leaf $original) -ErrorAction Stop
                [void]$script:PatchySetAsideFiles.Remove($setAside)
            } catch {
                Write-Warning "Could not restore ${original}: $($_.Exception.Message)"
            }
        }
    }
}

# Set-aside files that are still on disk go back into the manifest the install just wrote.
function Add-PatchySetAsideFilesToManifest {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [string[]]$EarlierSetAsideFiles = @()
    )

    foreach ($relativePath in (@($script:PatchySetAsideFiles) + @($EarlierSetAsideFiles) | Sort-Object -Unique)) {
        if ($relativePath -and (Test-Path -LiteralPath (Join-Path $InstallRoot $relativePath) -PathType Leaf)) {
            Add-PatchyInstalledRelativePath -InstallRoot $InstallRoot -RelativePath $relativePath
        }
    }
}

# The apps whose patchy-mcp.exe connector runs from this install (the connector's parent
# process: "Claude", "Codex"), for the note Patchy shows after an update.
function Get-PatchyConnectorClientNames {
    param([Parameter(Mandatory = $true)][string]$InstallRoot)

    $names = @()
    try {
        $connectors = @(Get-CimInstance Win32_Process -Filter "Name = 'patchy-mcp.exe'" -OperationTimeoutSec 5 -ErrorAction Stop)
    } catch {
        return @()
    }
    foreach ($connector in $connectors) {
        if (-not $connector.ExecutablePath -or
            -not (Test-PathAtOrInsideRoot -Path $connector.ExecutablePath -Root $InstallRoot)) {
            continue
        }
        $name = "AI app"
        try {
            $parent = Get-Process -Id $connector.ParentProcessId -ErrorAction Stop
            $name = $parent.ProcessName.Substring(0, 1).ToUpperInvariant() + $parent.ProcessName.Substring(1)
            $product = $parent.MainModule.FileVersionInfo.ProductName
            if (-not [string]::IsNullOrWhiteSpace($product)) {
                $name = $product.Trim()
            }
        } catch {
        }
        $names += ($name -replace '[|\r\n]', ' ')
    }
    return @($names | Sort-Object -Unique)
}

# Run before anything is removed, so a file in use never leaves a half-replaced install:
# locked files are set aside, and only one that cannot even be renamed stops and asks.
function Confirm-PatchyFilesFreeForInstall {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [bool]$Quiet = $false,

        [object]$Owner = $null,

        [string[]]$ExtraRelativePaths = @()
    )

    while ($true) {
        try {
            Move-PatchyLockedInstalledFilesAside -InstallRoot $InstallRoot -ExtraRelativePaths $ExtraRelativePaths
            return
        } catch {
        }
        $lockedFile = Get-PatchyLockedInstalledFile -InstallRoot $InstallRoot -ExtraRelativePaths $ExtraRelativePaths
        if (-not $lockedFile) {
            return
        }
        if ($Quiet -or -not [Environment]::UserInteractive) {
            throw (Get-PatchyInstallerText "FileInUseQuietMessage")
        }
        $message = (Get-PatchyInstallerText "LockedFileRetryMessage") -f $lockedFile
        if (-not (Request-ClosePatchyRetry -Owner $Owner -Message $message)) {
            throw (New-Object System.OperationCanceledException (Get-PatchyInstallerText "InstallationCanceled"))
        }
    }
}

function Add-PatchyInstalledRelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    if (-not (Test-SafeRelativeInstallPath -RelativePath $RelativePath)) {
        throw "Unsafe install manifest path: $RelativePath"
    }

    $manifest = Join-Path $InstallRoot $ManifestFileName
    $paths = @()
    if (Test-Path -LiteralPath $manifest -PathType Leaf) {
        $paths = @(Get-Content -LiteralPath $manifest | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_) -and (Test-SafeRelativeInstallPath -RelativePath $_)
        })
    }

    if ($paths -notcontains $RelativePath) {
        $paths = @($paths) + $RelativePath
        Set-Content -LiteralPath $manifest -Value ($paths | Sort-Object -Unique) -Encoding ASCII
    }
}

# Expands and normalizes a typed install folder. Returns $null for anything that is not
# a fully qualified drive or UNC folder path (relative, drive-relative, invalid names).
function ConvertTo-PatchyInstallRoot {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $null
    }
    $expanded = [Environment]::ExpandEnvironmentVariables($Path.Trim().Trim('"').Trim())
    if ($expanded -notmatch '^([A-Za-z]:[\\/]|[\\/]{2}[^\\/?.][^\\/]*[\\/]+[^\\/]+)') {
        return $null
    }
    try {
        $fullPath = [IO.Path]::GetFullPath($expanded)
    } catch {
        return $null
    }
    $pathRoot = [IO.Path]::GetPathRoot($fullPath)
    if ($fullPath.Substring($pathRoot.Length) -match '[<>:"|?*\x00-\x1F]') {
        return $null
    }
    $trimChars = [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    if ($fullPath.TrimEnd($trimChars).Length -le $pathRoot.TrimEnd($trimChars).Length) {
        # A drive or share root keeps its separator ("C:\").
        return $pathRoot
    }
    return $fullPath.TrimEnd($trimChars)
}

function Test-PatchyInstallFolder {
    param([Parameter(Mandatory = $true)][string]$Folder)

    return (Test-Path -LiteralPath (Join-Path $Folder "patchy.exe") -PathType Leaf) -or
        (Test-Path -LiteralPath (Join-Path $Folder $ManifestFileName) -PathType Leaf)
}

# Returns $null when setup may install into $Path, or the message that says why not.
# Installing and uninstalling remove every empty folder under the install root, and the
# uninstaller then removes the root itself, so only a new folder, an empty one, or an
# earlier Patchy install is accepted: never a drive root, the Windows folder, or a
# folder of someone else's files such as the Desktop.
function Test-PatchyInstallLocation {
    param([string]$Path)

    $root = ConvertTo-PatchyInstallRoot -Path $Path
    if (-not $root -or $root -eq [IO.Path]::GetPathRoot($root)) {
        return Get-PatchyInstallerText "InstallFolderInvalid"
    }
    $windowsDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
    if ($windowsDirectory -and (Test-PathAtOrInsideRoot -Path $root -Root $windowsDirectory)) {
        return Get-PatchyInstallerText "InstallFolderInvalid"
    }
    if (Test-Path -LiteralPath $root -PathType Leaf) {
        return Get-PatchyInstallerText "InstallFolderInvalid"
    }
    if (Test-Path -LiteralPath $root -PathType Container) {
        $firstEntry = @(Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue | Select-Object -First 1)
        if ($firstEntry.Count -gt 0 -and -not (Test-PatchyInstallFolder -Folder $root)) {
            return (Get-PatchyInstallerText "InstallFolderNotEmpty") -f $root
        }
    }
    return $null
}

# Creates the install folder and writes a scratch file in it. Returns $null when that
# works, or the message to show. Folders it created for a failed probe are removed again.
function Test-PatchyInstallFolderWritable {
    param([Parameter(Mandatory = $true)][string]$InstallRoot)

    $created = @()
    $folder = $InstallRoot
    while ($folder -and -not (Test-Path -LiteralPath $folder)) {
        $created += $folder
        $folder = Split-Path -Parent $folder
    }
    try {
        [void][IO.Directory]::CreateDirectory($InstallRoot)
        $probe = Join-Path $InstallRoot ("patchy-setup-" + [guid]::NewGuid().ToString("N") + ".tmp")
        [IO.File]::WriteAllText($probe, "")
        [IO.File]::Delete($probe)
        return $null
    } catch {
        foreach ($createdFolder in $created) {
            if ((Test-Path -LiteralPath $createdFolder -PathType Container) -and
                -not (Get-ChildItem -LiteralPath $createdFolder -Force -ErrorAction SilentlyContinue)) {
                Remove-Item -LiteralPath $createdFolder -Force -ErrorAction SilentlyContinue
            }
        }
        return (Get-PatchyInstallerText "InstallFolderNotWritable") -f $InstallRoot
    }
}

# The folder the Browse button fills in: a chosen folder gets a Patchy folder inside it,
# unless it is already named Patchy or already holds a Patchy install.
function Resolve-PatchyBrowsedFolder {
    param([Parameter(Mandatory = $true)][string]$Folder)

    $root = ConvertTo-PatchyInstallRoot -Path $Folder
    if (-not $root) {
        return $Folder
    }
    if ($root -ne [IO.Path]::GetPathRoot($root) -and
        ((Split-Path -Leaf $root) -eq "Patchy" -or (Test-PatchyInstallFolder -Folder $root))) {
        return $root
    }
    return Join-Path $root "Patchy"
}

# The nearest existing folder at or above $Path, where the Browse dialog starts.
function Get-PatchyExistingAncestor {
    param([string]$Path)

    $folder = ConvertTo-PatchyInstallRoot -Path $Path
    while ($folder -and -not (Test-Path -LiteralPath $folder -PathType Container)) {
        $folder = Split-Path -Parent $folder
    }
    return $folder
}

# The folder of the install the uninstall entry names, when patchy.exe is still there.
function Get-PatchyRegisteredInstallRoot {
    param([Parameter(Mandatory = $true)][string]$UninstallKey)

    try {
        $location = (Get-ItemProperty -LiteralPath $UninstallKey -Name "InstallLocation" -ErrorAction Stop).InstallLocation
    } catch {
        return $null
    }
    $root = ConvertTo-PatchyInstallRoot -Path $location
    if ($root -and (Test-Path -LiteralPath (Join-Path $root "patchy.exe") -PathType Leaf)) {
        return $root
    }
    return $null
}

# -InstallDir, else the folder of the existing install (an upgrade stays where the user
# put Patchy), else %LOCALAPPDATA%\Programs\Patchy.
function Get-PatchyDefaultInstallRoot {
    param(
        [string]$InstallDir,

        [Parameter(Mandatory = $true)]
        [string]$UninstallKey
    )

    if (-not [string]::IsNullOrWhiteSpace($InstallDir)) {
        return $InstallDir
    }
    $registered = Get-PatchyRegisteredInstallRoot -UninstallKey $UninstallKey
    if ($registered) {
        return $registered
    }
    return Join-Path (Join-Path $env:LOCALAPPDATA "Programs") "Patchy"
}

# Extensions Patchy is offered for in Explorer's "Open with" list. Keep in step with
# file_format_entries() in src/ui/main_window_files.cpp and the camera raw list in
# src/formats/raw_document_io.cpp. PDF is left out on purpose.
$PatchyOpenWithExtensions = @(
    "psd", "psb", "png", "apng", "jpg", "jpeg", "bmp", "tif", "tiff", "webp", "gif",
    "aseprite", "ase", "tga", "ico", "cur", "pcx", "lbm", "iff", "bbm", "pbm", "pgm", "ppm", "pnm",
    "svg", "svgz",
    "heic", "heif", "hif", "jxr", "wdp", "hdp", "jxl", "rttex", "dds",
    "hdr", "rgbe", "exr", "jp2", "j2k", "j2c", "jpc", "jpx", "jpf", "avif", "avifs",
    "af", "afphoto", "afdesign", "afpub", "pdn",
    "dng", "cr2", "cr3", "crw", "nef", "nrw", "arw", "sr2", "srf", "orf", "raf", "rw2",
    "pef", "srw", "mrw", "3fr", "fff", "iiq", "erf", "kdc", "dcr", "mos", "rwl", "x3f"
)

# The ProgID named in each extension's OpenWithProgids list. A persisted identifier:
# UninstallPatchy.cs and installed machines know it by this name.
$PatchyOpenWithProgId = "Patchy.Image"

# Removes what Register-PatchyOpenWith wrote. The extensions come from the registered
# SupportedTypes list, so an upgrade also clears types a newer list no longer has. An
# extension key is deleted only when removing Patchy's value leaves it empty.
function Unregister-PatchyOpenWith {
    param(
        [string]$ClassesRoot = "HKCU:\Software\Classes"
    )

    $applicationKey = Join-Path $ClassesRoot "Applications\patchy.exe"
    $supportedTypesKey = Join-Path $applicationKey "SupportedTypes"
    if (Test-Path -LiteralPath $supportedTypesKey) {
        foreach ($extension in @((Get-Item -LiteralPath $supportedTypesKey).GetValueNames())) {
            if ($extension -notmatch '^\.[A-Za-z0-9]+$') { continue }
            $extensionKey = Join-Path $ClassesRoot $extension
            $progIdsKey = Join-Path $extensionKey "OpenWithProgids"
            if (-not (Test-Path -LiteralPath $progIdsKey)) { continue }
            Remove-ItemProperty -LiteralPath $progIdsKey -Name $PatchyOpenWithProgId -ErrorAction SilentlyContinue
            foreach ($emptyCandidate in @($progIdsKey, $extensionKey)) {
                $key = Get-Item -LiteralPath $emptyCandidate
                if ($key.ValueCount -eq 0 -and $key.SubKeyCount -eq 0) {
                    Remove-Item -LiteralPath $emptyCandidate -Force
                } else {
                    break
                }
            }
        }
    }

    foreach ($ownedKey in @($applicationKey, (Join-Path $ClassesRoot $PatchyOpenWithProgId))) {
        if (Test-Path -LiteralPath $ownedKey) {
            Remove-Item -LiteralPath $ownedKey -Recurse -Force
        }
    }
}

# Lists Patchy as a choice under "Open with" for the types above, without making it the
# default for any of them. Two parts: the Applications\<exe> key names the app, and a
# Patchy ProgID added to each extension's OpenWithProgids list is what puts it in the
# "Open with" submenu (the Applications key alone only reaches "Choose another app").
# An extension's own default value and the user's choice are never written.
# UninstallPatchy.exe removes all of it.
function Register-PatchyOpenWith {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstalledExe,

        [string]$ClassesRoot = "HKCU:\Software\Classes"
    )

    Unregister-PatchyOpenWith -ClassesRoot $ClassesRoot

    $command = "`"$InstalledExe`" `"%1`""
    $applicationKey = Join-Path $ClassesRoot "Applications\patchy.exe"
    $supportedTypesKey = Join-Path $applicationKey "SupportedTypes"
    $progIdKey = Join-Path $ClassesRoot $PatchyOpenWithProgId
    foreach ($commandOwner in @($applicationKey, $progIdKey)) {
        $commandKey = Join-Path $commandOwner "shell\open\command"
        New-Item -Path $commandKey -Force | Out-Null
        Set-ItemProperty -Path $commandKey -Name "(default)" -Value $command
    }
    New-Item -Path $supportedTypesKey -Force | Out-Null
    New-ItemProperty -Path $applicationKey -Name "FriendlyAppName" -Value "Patchy Image Editor" -PropertyType String -Force | Out-Null
    foreach ($extension in $PatchyOpenWithExtensions) {
        New-ItemProperty -Path $supportedTypesKey -Name ".$extension" -Value "" -PropertyType String -Force | Out-Null
        $progIdsKey = Join-Path $ClassesRoot ".$extension\OpenWithProgids"
        if (-not (Test-Path -LiteralPath $progIdsKey)) {
            New-Item -Path $progIdsKey -Force | Out-Null
        }
        New-ItemProperty -Path $progIdsKey -Name $PatchyOpenWithProgId -Value "" -PropertyType String -Force | Out-Null
    }
}

# Tells Explorer the registration changed, so the entry appears without a sign-out.
function Send-PatchyAssociationChanged {
    Add-Type -Namespace PatchySetup -Name Shell -MemberDefinition @"
[DllImport("shell32.dll")]
public static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);
"@
    # SHCNE_ASSOCCHANGED, SHCNF_IDLIST
    [PatchySetup.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
}

# Starts the installed Patchy so its window comes to the front. Windows lets a new
# process take the foreground only while the process that started it owns it, so call
# this while setup's own window is still up, never after closing it (Patchy then opens
# under other windows). The grant covers a Patchy that hands the launch to an instance
# already running. Returns once Patchy has a window, it exits, or the wait runs out.
# Only a failure to start Patchy throws.
function Start-PatchyInForeground {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstalledExe,

        [int]$TimeoutSeconds = 10
    )

    # Not Start-Process: its -WorkingDirectory treats [ ] in a folder name as a wildcard.
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $InstalledExe
    $startInfo.WorkingDirectory = Split-Path -Parent $InstalledExe
    $startInfo.UseShellExecute = $false
    $process = [System.Diagnostics.Process]::Start($startInfo)

    try {
        if (-not ("PatchySetup.Foreground" -as [type])) {
            Add-Type -Namespace PatchySetup -Name Foreground -MemberDefinition @"
[DllImport("user32.dll")]
public static extern bool AllowSetForegroundWindow(uint processId);
"@
        }
        # ASFW_ANY
        [void][PatchySetup.Foreground]::AllowSetForegroundWindow([uint32]::MaxValue)

        $pumpMessages = [bool]("System.Windows.Forms.Application" -as [type])
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        while ([DateTime]::UtcNow -lt $deadline) {
            if ($process.WaitForExit(50)) { break }
            $process.Refresh()
            if ($process.MainWindowHandle -ne [IntPtr]::Zero) { break }
            if ($pumpMessages) {
                [System.Windows.Forms.Application]::DoEvents()
            }
        }
    } catch {
        # Patchy is running; where its window lands is not worth failing setup over.
    } finally {
        $process.Dispose()
    }
}

function Invoke-PatchyInstall {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PayloadZip,

        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [Parameter(Mandatory = $true)]
        [string]$StartMenuDirectory,

        [Parameter(Mandatory = $true)]
        [string]$StartMenuShortcut,

        [Parameter(Mandatory = $true)]
        [string]$DesktopShortcut,

        [bool]$CreateDesktopShortcut = $true,

        [Parameter(Mandatory = $true)]
        [string]$UninstallKey,

        [Parameter(Mandatory = $true)]
        [string]$Version,

        [bool]$Quiet = $false,

        [object]$Owner = $null
    )

    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("PatchyInstall-" + [guid]::NewGuid().ToString("N"))

    try {
        if (-not (Test-Path -LiteralPath $PayloadZip -PathType Leaf)) {
            throw "Installer payload was not found: $PayloadZip"
        }

        $locationProblem = Test-PatchyInstallLocation -Path $InstallRoot
        if ($locationProblem) {
            throw $locationProblem
        }
        $InstallRoot = ConvertTo-PatchyInstallRoot -Path $InstallRoot

        # An install registered in another folder moves here: its files go once this
        # install has succeeded. Nested folders are left alone rather than untangled.
        $previousRoot = Get-PatchyRegisteredInstallRoot -UninstallKey $UninstallKey
        if ($previousRoot -and ((Test-PathAtOrInsideRoot -Path $previousRoot -Root $InstallRoot) -or
                (Test-PathAtOrInsideRoot -Path $InstallRoot -Root $previousRoot))) {
            $previousRoot = $null
        }

        Confirm-PatchyClosedForInstall -InstallRoot $InstallRoot -Quiet $Quiet -Owner $Owner
        if ($previousRoot) {
            Confirm-PatchyClosedForInstall -InstallRoot $previousRoot -Quiet $Quiet -Owner $Owner
        }
        $writeProblem = Test-PatchyInstallFolderWritable -InstallRoot $InstallRoot
        if ($writeProblem) {
            throw $writeProblem
        }
        New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

        Expand-Archive -LiteralPath $PayloadZip -DestinationPath $tempRoot -Force
        $sourceRoot = Join-Path $tempRoot "Patchy"
        $sourceExe = Join-Path $sourceRoot "patchy.exe"
        if (-not (Test-Path -LiteralPath $sourceExe -PathType Leaf)) {
            throw "Installer payload does not contain Patchy\patchy.exe."
        }

        $payloadPaths = @()
        $payloadManifest = Join-Path $sourceRoot $ManifestFileName
        if (Test-Path -LiteralPath $payloadManifest -PathType Leaf) {
            $payloadPaths = @(Get-Content -LiteralPath $payloadManifest | Where-Object {
                -not [string]::IsNullOrWhiteSpace($_) -and (Test-SafeRelativeInstallPath -RelativePath $_)
            })
        }

        $earlierSetAside = @()
        if (Test-Path -LiteralPath $InstallRoot -PathType Container) {
            $earlierSetAside = @(Get-InstalledRelativePaths -InstallRoot $InstallRoot | Where-Object { $_ -match $SetAsideFilePattern })
        }
        # Up to here nothing installed has changed: the payload is unpacked and checked.
        # Files in use are set aside only now, and get their names back if the replacement
        # below does not go through.
        try {
            Confirm-PatchyFilesFreeForInstall -InstallRoot $InstallRoot -Quiet $Quiet -Owner $Owner -ExtraRelativePaths $payloadPaths
            Remove-PatchyInstalledFiles -InstallRoot $InstallRoot
            [void][IO.Directory]::CreateDirectory($InstallRoot)
            Get-ChildItem -LiteralPath $sourceRoot -Force | ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $InstallRoot -Recurse -Force
            }
        } catch {
            Restore-PatchySetAsideFiles -InstallRoot $InstallRoot
            throw
        }

        $payloadDirectory = Split-Path -Parent $PayloadZip
        $uninstallerSource = Join-Path $payloadDirectory "UninstallPatchy.exe"
        $installedExe = Join-Path $InstallRoot "patchy.exe"
        $installedIcon = Join-Path $InstallRoot "Patchy.ico"
        $uninstallerExe = Join-Path $InstallRoot "UninstallPatchy.exe"
        if (Test-Path -LiteralPath $uninstallerSource -PathType Leaf) {
            Copy-Item -LiteralPath $uninstallerSource -Destination $uninstallerExe -Force
        }
        if (-not (Test-Path -LiteralPath $uninstallerExe -PathType Leaf)) {
            throw "Installer payload does not contain Patchy\UninstallPatchy.exe."
        }
        Add-PatchyInstalledRelativePath -InstallRoot $InstallRoot -RelativePath "UninstallPatchy.exe"
        Add-PatchySetAsideFilesToManifest -InstallRoot $InstallRoot -EarlierSetAsideFiles $earlierSetAside

        New-Item -ItemType Directory -Path $StartMenuDirectory -Force | Out-Null
        New-PatchyShortcut -ShortcutPath $StartMenuShortcut -TargetPath $installedExe -WorkingDirectory $InstallRoot -IconPath $installedIcon
        if ($CreateDesktopShortcut) {
            try {
                $desktopDirectory = Split-Path -Parent $DesktopShortcut
                if (-not [string]::IsNullOrWhiteSpace($desktopDirectory)) {
                    New-Item -ItemType Directory -Path $desktopDirectory -Force | Out-Null
                }
                New-PatchyShortcut -ShortcutPath $DesktopShortcut -TargetPath $installedExe -WorkingDirectory $InstallRoot -IconPath $installedIcon
            } catch {
                Write-Warning "Could not create the desktop shortcut: $($_.Exception.Message)"
            }
        }

        $estimatedSizeKb = [int][math]::Ceiling(
            ((Get-ChildItem -LiteralPath $InstallRoot -Recurse -File | Measure-Object -Property Length -Sum).Sum) / 1KB
        )

        New-Item -Path $UninstallKey -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "DisplayName" -Value "Patchy" -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "DisplayVersion" -Value $Version -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "Publisher" -Value "Seth A. Robinson" -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "DisplayIcon" -Value $installedIcon -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "InstallLocation" -Value $InstallRoot -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "UninstallString" -Value "`"$uninstallerExe`"" -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "QuietUninstallString" -Value "`"$uninstallerExe`" /quiet" -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "NoModify" -Value 1 -PropertyType DWord -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "NoRepair" -Value 1 -PropertyType DWord -Force | Out-Null
        New-ItemProperty -Path $UninstallKey -Name "EstimatedSize" -Value $estimatedSizeKb -PropertyType DWord -Force | Out-Null

        try {
            Register-PatchyOpenWith -InstalledExe $installedExe
            Send-PatchyAssociationChanged
        } catch {
            Write-Warning "Could not add Patchy to the Open with list: $($_.Exception.Message)"
        }

        if ($previousRoot) {
            try {
                Remove-PatchyInstalledFiles -InstallRoot $previousRoot
                if (-not (Get-ChildItem -LiteralPath $previousRoot -Force -ErrorAction SilentlyContinue)) {
                    Remove-Item -LiteralPath $previousRoot -Force -ErrorAction SilentlyContinue
                }
            } catch {
                Write-Warning "Could not remove the earlier install in ${previousRoot}: $($_.Exception.Message)"
            }
        }

        return $installedExe
    } finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Invoke-PatchyInstallWithRetry {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PayloadZip,

        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [Parameter(Mandatory = $true)]
        [string]$StartMenuDirectory,

        [Parameter(Mandatory = $true)]
        [string]$StartMenuShortcut,

        [Parameter(Mandatory = $true)]
        [string]$DesktopShortcut,

        [bool]$CreateDesktopShortcut = $true,

        [Parameter(Mandatory = $true)]
        [string]$UninstallKey,

        [Parameter(Mandatory = $true)]
        [string]$Version,

        [bool]$Quiet = $false,

        [object]$Owner = $null
    )

    while ($true) {
        try {
            return Invoke-PatchyInstall `
                -PayloadZip $PayloadZip `
                -InstallRoot $InstallRoot `
                -StartMenuDirectory $StartMenuDirectory `
                -StartMenuShortcut $StartMenuShortcut `
                -DesktopShortcut $DesktopShortcut `
                -CreateDesktopShortcut $CreateDesktopShortcut `
                -UninstallKey $UninstallKey `
                -Version $Version `
                -Quiet $Quiet `
                -Owner $Owner
        } catch [System.OperationCanceledException] {
            throw
        } catch {
            if (-not (Test-PatchyFileInUseInstallError -ErrorRecord $_)) {
                throw
            }
            if ($Quiet -or -not [Environment]::UserInteractive) {
                throw (Get-PatchyInstallerText "FileInUseQuietMessage")
            }
            if (-not (Request-ClosePatchyRetry -Owner $Owner)) {
                throw (New-Object System.OperationCanceledException (Get-PatchyInstallerText "InstallationCanceled"))
            }
        }
    }
}

# Update mode has no wizard: a small window says what is happening while the files are
# replaced (several seconds). The label is English, like the wizard's own.
function Show-PatchyUpdateProgress {
    param(
        [string]$Version = "",

        [string]$IconPath = ""
    )

    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [System.Windows.Forms.Application]::EnableVisualStyles()

    $form = New-Object System.Windows.Forms.Form
    $form.Text = "Patchy Setup"
    $form.StartPosition = "CenterScreen"
    $form.FormBorderStyle = "FixedDialog"
    $form.ControlBox = $false
    $form.ClientSize = New-Object System.Drawing.Size 360, 96
    $form.Font = New-Object System.Drawing.Font "Segoe UI", 10
    $form.BackColor = [System.Drawing.Color]::White
    $form.UseWaitCursor = $true
    if ($IconPath -and (Test-Path -LiteralPath $IconPath -PathType Leaf)) {
        # Artwork is decoration: an icon that fails to load must never stop the update.
        try {
            $form.Icon = New-Object System.Drawing.Icon $IconPath
        } catch {
        }
    }
    $label = New-Object System.Windows.Forms.Label
    if ($Version -and $Version -ne "0.0.0") {
        $label.Text = "Updating Patchy to version $Version..."
    } else {
        $label.Text = "Updating Patchy..."
    }
    $label.Location = New-Object System.Drawing.Point 0, 18
    $label.Size = New-Object System.Drawing.Size 360, 28
    $label.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
    $form.Controls.Add($label)
    $progress = New-Object System.Windows.Forms.ProgressBar
    $progress.Style = [System.Windows.Forms.ProgressBarStyle]::Marquee
    $progress.MarqueeAnimationSpeed = 30
    $progress.Location = New-Object System.Drawing.Point 30, 58
    $progress.Size = New-Object System.Drawing.Size 300, 14
    $form.Controls.Add($progress)
    $form.Show()
    $form.Refresh()
    [System.Windows.Forms.Application]::DoEvents()
    return $form
}

function New-PatchyLogoBitmap {
    param([string]$IconPath, [int]$Size = 64)

    # Use the same authored artwork as the executable and installed shortcuts.
    # Decode the largest ICO frame directly before scaling it to the wizard's slot:
    # Icon.ToBitmap() cannot read PNG-compressed frames in Windows PowerShell 5.1 and
    # throws "Requested range extends past the end of the array" (issue 55).
    $bytes = [System.IO.File]::ReadAllBytes($IconPath)
    $count = [System.BitConverter]::ToUInt16($bytes, 4)
    $best = -1
    $bestWidth = 0
    for ($i = 0; $i -lt $count; $i++) {
        $width = [int]$bytes[6 + 16 * $i]
        if ($width -eq 0) { $width = 256 }
        if ($width -gt $bestWidth) { $bestWidth = $width; $best = $i }
    }
    if ($best -lt 0) { throw "No frames in $IconPath" }
    $length = [System.BitConverter]::ToInt32($bytes, 6 + 16 * $best + 8)
    $offset = [System.BitConverter]::ToInt32($bytes, 6 + 16 * $best + 12)
    $icon = $null
    $stream = $null
    if ($bytes[$offset] -eq 0x89 -and $bytes[$offset + 1] -eq 0x50 -and $bytes[$offset + 2] -eq 0x4E -and $bytes[$offset + 3] -eq 0x47) {
        $stream = New-Object System.IO.MemoryStream (, [byte[]]$bytes[$offset..($offset + $length - 1)])
        $source = [System.Drawing.Image]::FromStream($stream)
    }
    else {
        $icon = New-Object System.Drawing.Icon $IconPath, $bestWidth, $bestWidth
        $source = $icon.ToBitmap()
    }
    $bitmap = New-Object System.Drawing.Bitmap $Size, $Size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.DrawImage($source, 0, 0, $Size, $Size)
    }
    finally {
        $graphics.Dispose()
        $source.Dispose()
        if ($stream) { $stream.Dispose() }
        if ($icon) { $icon.Dispose() }
    }
    return $bitmap
}

function Show-PatchyInstallerWizard {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PayloadZip,

        [Parameter(Mandatory = $true)]
        [string]$InstallRoot,

        [Parameter(Mandatory = $true)]
        [string]$StartMenuDirectory,

        [Parameter(Mandatory = $true)]
        [string]$StartMenuShortcut,

        [Parameter(Mandatory = $true)]
        [string]$DesktopShortcut,

        [Parameter(Mandatory = $true)]
        [string]$UninstallKey,

        [Parameter(Mandatory = $true)]
        [string]$Version,

        [switch]$SmokeTest
    )

    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [System.Windows.Forms.Application]::EnableVisualStyles()

    $state = @{
        InstalledExe = $null
        Launch = $false
        Launched = $false
        Completed = $false
    }

    $form = New-Object System.Windows.Forms.Form
    $form.Text = "Patchy Setup"
    $form.StartPosition = "CenterScreen"
    $form.FormBorderStyle = "FixedDialog"
    $form.MaximizeBox = $false
    $form.MinimizeBox = $true
    $form.ClientSize = New-Object System.Drawing.Size 560, 380
    $form.Font = New-Object System.Drawing.Font "Segoe UI", 9
    $form.BackColor = [System.Drawing.Color]::White
    $form.Tag = "ready"
    $formIcon = $null
    $installerIconPath = Join-Path (Split-Path -Parent $PayloadZip) "Patchy.ico"
    if (Test-Path -LiteralPath $installerIconPath -PathType Leaf) {
        # Artwork is decoration: a logo that fails to load must never stop setup.
        try {
            $formIcon = New-Object System.Drawing.Icon $installerIconPath
            $form.Icon = $formIcon
        }
        catch {
            if ($SmokeTest) { throw }
            $formIcon = $null
        }
    }

    $leftPanel = New-Object System.Windows.Forms.Panel
    $leftPanel.BackColor = [System.Drawing.Color]::FromArgb(23, 30, 40)
    $leftPanel.Dock = [System.Windows.Forms.DockStyle]::Left
    $leftPanel.Width = 148
    $form.Controls.Add($leftPanel)

    $logo = New-Object System.Windows.Forms.PictureBox
    $logo.Size = New-Object System.Drawing.Size 74, 74
    $logo.Location = New-Object System.Drawing.Point 37, 42
    if (Test-Path -LiteralPath $installerIconPath -PathType Leaf) {
        try {
            $logo.Image = New-PatchyLogoBitmap -IconPath $installerIconPath -Size 74
        }
        catch {
            if ($SmokeTest) { throw }
            $logo.Image = $null
        }
    }
    $logo.SizeMode = [System.Windows.Forms.PictureBoxSizeMode]::CenterImage
    $leftPanel.Controls.Add($logo)

    $brand = New-Object System.Windows.Forms.Label
    $brand.Text = "Patchy"
    $brand.ForeColor = [System.Drawing.Color]::White
    $brand.BackColor = [System.Drawing.Color]::Transparent
    $brand.Font = New-Object System.Drawing.Font "Segoe UI Semibold", 18
    $brand.AutoSize = $true
    $brand.Location = New-Object System.Drawing.Point 36, 130
    $leftPanel.Controls.Add($brand)

    $contentLeft = 176
    $title = New-Object System.Windows.Forms.Label
    $title.Text = "Install Patchy $Version"
    $title.Font = New-Object System.Drawing.Font "Segoe UI Semibold", 15
    $title.ForeColor = [System.Drawing.Color]::FromArgb(23, 30, 40)
    $title.AutoSize = $true
    $title.Location = New-Object System.Drawing.Point $contentLeft, 34
    $form.Controls.Add($title)

    $body = New-Object System.Windows.Forms.Label
    $body.Text = "Setup will install Patchy for the current Windows user and add a Start Menu shortcut."
    $body.ForeColor = [System.Drawing.Color]::FromArgb(63, 72, 84)
    $body.Size = New-Object System.Drawing.Size 340, 42
    $body.Location = New-Object System.Drawing.Point $contentLeft, 74
    $form.Controls.Add($body)

    $pathLabel = New-Object System.Windows.Forms.Label
    $pathLabel.Text = "Install location"
    $pathLabel.ForeColor = [System.Drawing.Color]::FromArgb(63, 72, 84)
    $pathLabel.AutoSize = $true
    $pathLabel.Location = New-Object System.Drawing.Point $contentLeft, 132
    $form.Controls.Add($pathLabel)

    $pathBox = New-Object System.Windows.Forms.TextBox
    $pathBox.Text = $InstallRoot
    $pathBox.BorderStyle = [System.Windows.Forms.BorderStyle]::FixedSingle
    $pathBox.Location = New-Object System.Drawing.Point $contentLeft, 156
    $pathBox.Size = New-Object System.Drawing.Size 252, 24
    $form.Controls.Add($pathBox)

    $browseButton = New-Object System.Windows.Forms.Button
    $browseButton.Text = "Browse..."
    $browseButton.Size = New-Object System.Drawing.Size 86, ($pathBox.Height + 2)
    $browseButton.Location = New-Object System.Drawing.Point ($pathBox.Right + 6), ($pathBox.Top - 1)
    $browseButton.UseVisualStyleBackColor = $true
    $form.Controls.Add($browseButton)

    $desktopShortcutCheck = New-Object System.Windows.Forms.CheckBox
    $desktopShortcutCheck.Text = "Create a desktop shortcut"
    $desktopShortcutCheck.Checked = $true
    $desktopShortcutCheck.AutoSize = $true
    $desktopShortcutCheck.Location = New-Object System.Drawing.Point $contentLeft, 190
    $form.Controls.Add($desktopShortcutCheck)

    $legalNotice = New-Object System.Windows.Forms.Label
    $legalNotice.Text = "Patchy is provided under the MIT License as-is, without warranty. Keep backups of important files."
    $legalNotice.ForeColor = [System.Drawing.Color]::FromArgb(83, 92, 104)
    $legalNotice.Size = New-Object System.Drawing.Size 344, 40
    $legalNotice.Location = New-Object System.Drawing.Point $contentLeft, 218
    $form.Controls.Add($legalNotice)

    $status = New-Object System.Windows.Forms.Label
    $status.Text = ""
    $status.ForeColor = [System.Drawing.Color]::FromArgb(63, 72, 84)
    $status.Size = New-Object System.Drawing.Size 344, 24
    # Keep separate rows for the legal notice, status, and progress bar.
    $status.Location = New-Object System.Drawing.Point $contentLeft, ($legalNotice.Bottom + 8)
    $form.Controls.Add($status)

    $progress = New-Object System.Windows.Forms.ProgressBar
    $progress.Location = New-Object System.Drawing.Point $contentLeft, ($status.Bottom + 6)
    $progress.Size = New-Object System.Drawing.Size 344, 18
    $progress.Style = [System.Windows.Forms.ProgressBarStyle]::Marquee
    $progress.MarqueeAnimationSpeed = 30
    $progress.Visible = $false
    $form.Controls.Add($progress)

    $launchCheck = New-Object System.Windows.Forms.CheckBox
    $launchCheck.Text = "Launch Patchy now"
    $launchCheck.Checked = $true
    $launchCheck.AutoSize = $true
    $launchCheck.Location = New-Object System.Drawing.Point $contentLeft, 158
    $launchCheck.Visible = $false
    $form.Controls.Add($launchCheck)

    $buttonPanel = New-Object System.Windows.Forms.Panel
    $buttonPanel.Height = 58
    $buttonPanel.Dock = [System.Windows.Forms.DockStyle]::Bottom
    $buttonPanel.BackColor = [System.Drawing.Color]::FromArgb(246, 248, 251)
    $form.Controls.Add($buttonPanel)

    $installButton = New-Object System.Windows.Forms.Button
    $installButton.Text = "Install"
    $installButton.Size = New-Object System.Drawing.Size 92, 30
    $installButton.Location = New-Object System.Drawing.Point 356, 14
    $installButton.UseVisualStyleBackColor = $true
    $buttonPanel.Controls.Add($installButton)

    $cancelButton = New-Object System.Windows.Forms.Button
    $cancelButton.Text = "Cancel"
    $cancelButton.Size = New-Object System.Drawing.Size 92, 30
    $cancelButton.Location = New-Object System.Drawing.Point 456, 14
    $cancelButton.UseVisualStyleBackColor = $true
    $buttonPanel.Controls.Add($cancelButton)

    $installButton.Add_Click({
        if ($form.Tag -eq "complete") {
            $state.Launch = $launchCheck.Checked
            if ($state.Launch -and (Test-Path -LiteralPath $state.InstalledExe -PathType Leaf)) {
                # Before the wizard closes, so Patchy can come to the front.
                $installButton.Enabled = $false
                $launchCheck.Enabled = $false
                try {
                    Start-PatchyInForeground -InstalledExe $state.InstalledExe
                    $state.Launched = $true
                } catch {
                    # The caller tries once more after the wizard closes.
                }
            }
            $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
            $form.Close()
            return
        }

        $locationProblem = Test-PatchyInstallLocation -Path $pathBox.Text
        if ($locationProblem) {
            [System.Windows.Forms.MessageBox]::Show(
                $form,
                $locationProblem,
                "Patchy Setup",
                [System.Windows.Forms.MessageBoxButtons]::OK,
                [System.Windows.Forms.MessageBoxIcon]::Warning
            ) | Out-Null
            $pathBox.Focus() | Out-Null
            $pathBox.SelectAll()
            return
        }

        $installButton.Enabled = $false
        $cancelButton.Enabled = $false
        $pathBox.Enabled = $false
        $browseButton.Enabled = $false
        $progress.Visible = $true
        $status.Text = "Installing Patchy..."
        $form.UseWaitCursor = $true
        $form.Refresh()
        [System.Windows.Forms.Application]::DoEvents()

        try {
            $state.InstalledExe = Invoke-PatchyInstallWithRetry `
                -PayloadZip $PayloadZip `
                -InstallRoot $pathBox.Text `
                -StartMenuDirectory $StartMenuDirectory `
                -StartMenuShortcut $StartMenuShortcut `
                -DesktopShortcut $DesktopShortcut `
                -CreateDesktopShortcut $desktopShortcutCheck.Checked `
                -UninstallKey $UninstallKey `
                -Version $Version `
                -Owner $form

            $state.Completed = $true
            $form.Tag = "complete"
            $title.Text = "Patchy has been installed"
            $body.Text = "Setup finished installing Patchy on this computer."
            $pathLabel.Visible = $false
            $pathBox.Visible = $false
            $browseButton.Visible = $false
            $desktopShortcutCheck.Visible = $false
            $status.Text = ""
            $launchCheck.Visible = $true
            $installButton.Text = "Finish"
            $cancelButton.Visible = $false
        } catch [System.OperationCanceledException] {
            $status.Text = Get-PatchyInstallerText "InstallationCanceled"
            $cancelButton.Enabled = $true
        } catch {
            $status.Text = "Installation failed."
            $cancelButton.Enabled = $true
            [System.Windows.Forms.MessageBox]::Show(
                $form,
                $_.Exception.Message,
                "Patchy Setup",
                [System.Windows.Forms.MessageBoxButtons]::OK,
                [System.Windows.Forms.MessageBoxIcon]::Error
            ) | Out-Null
        } finally {
            $progress.Visible = $false
            $form.UseWaitCursor = $false
            $installButton.Enabled = $true
            $pathBox.Enabled = $true
            $browseButton.Enabled = $true
        }
    })

    $browseButton.Add_Click({
        $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
        $dialog.Description = "Choose where to install Patchy. Setup installs into a Patchy folder inside the folder you choose."
        $dialog.ShowNewFolderButton = $true
        $startFolder = Get-PatchyExistingAncestor -Path $pathBox.Text
        if ($startFolder) {
            $dialog.SelectedPath = $startFolder
        }
        try {
            if ($dialog.ShowDialog($form) -eq [System.Windows.Forms.DialogResult]::OK -and $dialog.SelectedPath) {
                $pathBox.Text = Resolve-PatchyBrowsedFolder -Folder $dialog.SelectedPath
            }
        } finally {
            $dialog.Dispose()
        }
    })

    $cancelButton.Add_Click({
        $form.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
        $form.Close()
    })

    $form.AcceptButton = $installButton
    $form.CancelButton = $cancelButton
    if ($SmokeTest) {
        # Same form and controls as a real run, invisible, closed as soon as it is shown.
        $form.Opacity = 0
        $form.ShowInTaskbar = $false
        $form.Add_Shown({
            $form.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
            $form.Close()
        })
    }
    [void]$form.ShowDialog()

    if ($logo.Image) {
        $logo.Image.Dispose()
    }
    if ($formIcon) {
        $formIcon.Dispose()
    }
    $form.Dispose()

    return [pscustomobject]$state
}

$startMenuDirectory = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
$startMenuShortcut = Join-Path $startMenuDirectory "Patchy.lnk"
$desktopShortcut = Join-Path ([System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::DesktopDirectory)) "Patchy.lnk"
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Patchy"
$installRoot = Get-PatchyDefaultInstallRoot -InstallDir $InstallDir -UninstallKey $uninstallKey

if ($CheckLogo) {
    # build-release.bat runs this in Windows PowerShell 5.1, the host the installer uses.
    Add-Type -AssemblyName System.Drawing
    $checkIcon = Join-Path (Split-Path -Parent $PayloadZip) "Patchy.ico"
    $logoBitmap = New-PatchyLogoBitmap -IconPath $checkIcon -Size 74
    $windowIcon = New-Object System.Drawing.Icon $checkIcon
    Write-Host "Installer logo check passed ($($logoBitmap.Width) x $($logoBitmap.Height))."
    exit 0
}

if ($OpenWithCheckRoot) {
    # Only a scratch key: the real Classes tree is never a valid target for the check.
    if ($OpenWithCheckRoot -notlike "HKCU:\Software\PatchyVerify-*") {
        Write-Host "Open with check needs a HKCU:\Software\PatchyVerify-* scratch key."
        exit 1
    }
    try {
        Register-PatchyOpenWith -InstalledExe (Join-Path $installRoot "patchy.exe") -ClassesRoot $OpenWithCheckRoot
        Write-Host "Open with registration check wrote $OpenWithCheckRoot."
        exit 0
    } catch {
        Write-Host "Open with registration check FAILED: $($_.Exception.Message)"
        exit 1
    }
}

if ($LocationCheckRoot) {
    # Builds its cases inside the scratch folder; nothing is installed or registered.
    $failures = @()
    try {
        $checkRoot = ConvertTo-PatchyInstallRoot -Path $LocationCheckRoot
        if (-not $checkRoot -or $checkRoot -eq [IO.Path]::GetPathRoot($checkRoot)) {
            throw "Location check needs a full scratch folder path."
        }
        $emptyFolder = Join-Path $checkRoot "empty"
        $foreignFolder = Join-Path $checkRoot "foreign"
        $previousFolder = Join-Path $checkRoot "tools"
        $fileTarget = Join-Path $checkRoot "file.txt"
        $newFolder = Join-Path $checkRoot "new\Patchy"
        foreach ($folder in @($emptyFolder, $foreignFolder, $previousFolder)) {
            [void][IO.Directory]::CreateDirectory($folder)
        }
        [IO.File]::WriteAllText((Join-Path $foreignFolder "notes.txt"), "not Patchy")
        [IO.File]::WriteAllText((Join-Path $previousFolder $ManifestFileName), "patchy.exe")
        [IO.File]::WriteAllText($fileTarget, "")

        $windowsDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
        $cases = @(
            @{ Path = $newFolder; Accepted = $true },
            @{ Path = $emptyFolder + "\"; Accepted = $true },
            @{ Path = $previousFolder; Accepted = $true },
            @{ Path = $foreignFolder; Accepted = $false },
            @{ Path = $fileTarget; Accepted = $false },
            @{ Path = [IO.Path]::GetPathRoot($checkRoot); Accepted = $false },
            @{ Path = "Patchy"; Accepted = $false },
            @{ Path = "C:Patchy"; Accepted = $false },
            @{ Path = ""; Accepted = $false },
            @{ Path = (Join-Path $checkRoot "bad|name"); Accepted = $false },
            @{ Path = (Join-Path $windowsDirectory "Patchy"); Accepted = $false }
        )
        foreach ($case in $cases) {
            $problem = Test-PatchyInstallLocation -Path $case.Path
            if (($problem -eq $null) -ne $case.Accepted) {
                $failures += "'$($case.Path)' accepted=$($problem -eq $null): $problem"
            }
        }
        $foreignProblem = Test-PatchyInstallLocation -Path $foreignFolder
        if ($foreignProblem -and -not $foreignProblem.Contains($foreignFolder)) {
            $failures += "the not-empty message does not name the folder: $foreignProblem"
        }

        $writeProblem = Test-PatchyInstallFolderWritable -InstallRoot $newFolder
        if ($writeProblem) {
            $failures += "write probe failed in a new folder: $writeProblem"
        } elseif (@(Get-ChildItem -LiteralPath $newFolder -Force).Count -ne 0) {
            $failures += "write probe left files in $newFolder"
        }

        $browseCases = @(
            @{ Folder = (Join-Path $checkRoot "apps"); Expected = (Join-Path $checkRoot "apps\Patchy") },
            @{ Folder = (Join-Path $checkRoot "apps\Patchy"); Expected = (Join-Path $checkRoot "apps\Patchy") },
            @{ Folder = $previousFolder; Expected = $previousFolder },
            @{ Folder = [IO.Path]::GetPathRoot($checkRoot); Expected = (Join-Path ([IO.Path]::GetPathRoot($checkRoot)) "Patchy") }
        )
        foreach ($case in $browseCases) {
            $resolved = Resolve-PatchyBrowsedFolder -Folder $case.Folder
            if ($resolved -ne $case.Expected) {
                $failures += "Browse '$($case.Folder)' gave '$resolved', expected '$($case.Expected)'"
            }
        }
    } catch {
        $failures += $_.Exception.Message
    }
    if ($failures.Count -gt 0) {
        Write-Host ("Install location check FAILED: " + ($failures -join "; "))
        exit 1
    }
    Write-Host "Install location check passed."
    exit 0
}

if ($UpdateCheckRoot) {
    # Builds its cases inside the scratch folder; nothing is installed, registered or relaunched.
    $failures = @()
    try {
        $checkRoot = ConvertTo-PatchyInstallRoot -Path $UpdateCheckRoot
        if (-not $checkRoot -or $checkRoot -eq [IO.Path]::GetPathRoot($checkRoot)) {
            throw "Update check needs a full scratch folder path."
        }
        $installFolder = Join-Path $checkRoot "install"
        [void][IO.Directory]::CreateDirectory($installFolder)
        $connector = Join-Path $installFolder "patchy-mcp.exe"
        [IO.File]::WriteAllText((Join-Path $installFolder "patchy.exe"), "stand-in")
        [IO.File]::WriteAllText($connector, "stand-in")
        [IO.File]::WriteAllLines((Join-Path $installFolder $ManifestFileName), @("patchy.exe", "patchy-mcp.exe"))

        if (Get-PatchyLockedInstalledFile -InstallRoot $installFolder) {
            $failures += "an install with no file in use reported a locked file"
        }
        $held = [IO.File]::Open($connector, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        try {
            $locked = Get-PatchyLockedInstalledFile -InstallRoot $installFolder
            if ($locked -ne "patchy-mcp.exe") {
                $failures += "a held patchy-mcp.exe was reported as '$locked'"
            }
            $refused = $false
            try {
                Confirm-PatchyFilesFreeForInstall -InstallRoot $installFolder -Quiet $true
            } catch {
                $refused = $true
            }
            if (-not $refused) {
                $failures += "a quiet install went ahead with a file in use"
            }
        } finally {
            $held.Dispose()
        }
        if (Get-PatchyLockedInstalledFile -InstallRoot $installFolder) {
            $failures += "a released file was still reported as locked"
        }
        if ($script:PatchySetAsideFiles.Count -ne 0) {
            $failures += "a file that could not be renamed was recorded as set aside"
        }

        # Held the way a running program holds its image: in use, but it can be renamed.
        $held = [IO.File]::Open($connector, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            ([IO.FileShare]::Read -bor [IO.FileShare]::Delete))
        try {
            Confirm-PatchyFilesFreeForInstall -InstallRoot $installFolder -Quiet $true
            $setAside = @($script:PatchySetAsideFiles)
            if ($setAside.Count -ne 1 -or $setAside[0] -notmatch '^patchy-mcp\.exe\.old-\d{14}$') {
                $failures += "the connector in use was not set aside: '$($setAside -join ', ')'"
            } elseif ((Test-Path -LiteralPath $connector) -or
                      -not (Test-Path -LiteralPath (Join-Path $installFolder $setAside[0]) -PathType Leaf)) {
                $failures += "the set-aside connector is not where it should be"
            } else {
                [IO.File]::WriteAllText($connector, "new version")
                Add-PatchySetAsideFilesToManifest -InstallRoot $installFolder
                if ((Get-InstalledRelativePaths -InstallRoot $installFolder) -notcontains $setAside[0]) {
                    $failures += "the set-aside connector is missing from the install manifest"
                }
                if (Get-PatchyLockedInstalledFile -InstallRoot $installFolder) {
                    $failures += "a set-aside file still counted as a file in use"
                }
            }
        } finally {
            $held.Dispose()
        }
        # Once nothing uses it, the next install removes it with the other manifest files.
        Remove-PatchyInstalledFiles -InstallRoot $installFolder
        if (@(Get-ChildItem -LiteralPath $checkRoot -Recurse -File -Filter "*.old-*").Count -ne 0) {
            $failures += "a set-aside file survived the next install's cleanup"
        }
        [IO.File]::WriteAllText((Join-Path $installFolder "patchy.exe"), "stand-in")
        if (-not (Test-Path -LiteralPath (Join-Path $installFolder "patchy.exe") -PathType Leaf)) {
            $failures += "the locked-file check removed an installed file"
        }

        # An install with no manifest: a file in use is found through the new version's file
        # list, and a replacement that fails gives the set-aside file its name back.
        $script:PatchySetAsideFiles.Clear()
        $bareFolder = Join-Path $checkRoot "no-manifest"
        [void][IO.Directory]::CreateDirectory($bareFolder)
        $runtime = Join-Path $bareFolder "Qt6Extra.dll"
        [IO.File]::WriteAllText($runtime, "previous version")
        $held = [IO.File]::Open($runtime, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            ([IO.FileShare]::Read -bor [IO.FileShare]::Delete))
        try {
            if (Get-PatchyLockedInstalledFile -InstallRoot $bareFolder) {
                $failures += "a file outside the legacy list was reported without the new file list"
            }
            if ((Get-PatchyLockedInstalledFile -InstallRoot $bareFolder -ExtraRelativePaths @("Qt6Extra.dll")) -ne "Qt6Extra.dll") {
                $failures += "a file in use was not found through the new version's file list"
            }
            Confirm-PatchyFilesFreeForInstall -InstallRoot $bareFolder -Quiet $true -ExtraRelativePaths @("Qt6Extra.dll")
            if ((Test-Path -LiteralPath $runtime) -or $script:PatchySetAsideFiles.Count -ne 1) {
                $failures += "the file in use was not set aside in the install without a manifest"
            }
            Restore-PatchySetAsideFiles -InstallRoot $bareFolder
            if (-not (Test-Path -LiteralPath $runtime -PathType Leaf) -or $script:PatchySetAsideFiles.Count -ne 0 -or
                @(Get-ChildItem -LiteralPath $bareFolder -File -Filter "*.old-*").Count -ne 0) {
                $failures += "a failed install did not give the set-aside file its name back"
            }
        } finally {
            $held.Dispose()
        }

        $child = Start-Process -FilePath (Join-Path $PSHOME "powershell.exe") `
            -ArgumentList "-NoLogo -NoProfile -Command Start-Sleep -Seconds 3" -WindowStyle Hidden -PassThru
        if (Wait-PatchyProcessExit -ProcessId $child.Id -TimeoutSeconds 0) {
            $failures += "a running process was reported as exited"
        }
        if (-not (Wait-PatchyProcessExit -ProcessId $child.Id -TimeoutSeconds 30)) {
            $failures += "the wait did not see the process exit"
        }
        if (-not (Wait-PatchyProcessExit -ProcessId $child.Id -TimeoutSeconds 0)) {
            $failures += "a process that is gone was reported as running"
        }

        # A requester with no window left: a hidden process running from a copy of
        # powershell.exe named patchy.exe inside the install folder is ended by its id; the
        # same thing outside the folder is not the requester and is left alone.
        $standInExe = Join-Path $installFolder "patchy.exe"
        Copy-Item -LiteralPath (Join-Path $PSHOME "powershell.exe") -Destination $standInExe -Force
        $sleepArguments = "-NoLogo -NoProfile -Command Start-Sleep -Seconds 60"
        $headless = Start-Process -FilePath $standInExe -ArgumentList $sleepArguments -WindowStyle Hidden -PassThru
        $outside = Start-Process -FilePath (Join-Path $PSHOME "powershell.exe") -ArgumentList $sleepArguments -WindowStyle Hidden -PassThru
        try {
            Start-Sleep -Milliseconds 500
            if (-not (Test-PatchyHeadlessUpdateRequester -ProcessId $headless.Id -InstallRoot $installFolder)) {
                $failures += "a hidden process from the install folder was not recognized as the requester"
            }
            if (Test-PatchyHeadlessUpdateRequester -ProcessId $outside.Id -InstallRoot $installFolder) {
                $failures += "a process outside the install folder counted as the requester"
            }
            if (Stop-PatchyHeadlessUpdateRequester -ProcessId $outside.Id -InstallRoot $installFolder) {
                $failures += "a process outside the install folder was ended"
            }
            if (-not (Stop-PatchyHeadlessUpdateRequester -ProcessId $headless.Id -InstallRoot $installFolder)) {
                $failures += "the headless requester was not ended"
            }
            if (-not (Wait-PatchyProcessExit -ProcessId $headless.Id -TimeoutSeconds 0)) {
                $failures += "the headless requester is still running"
            }
            if (Wait-PatchyProcessExit -ProcessId $outside.Id -TimeoutSeconds 0) {
                $failures += "the process outside the install folder is gone"
            }
            if (Test-PatchyHeadlessUpdateRequester -ProcessId $headless.Id -InstallRoot $installFolder) {
                $failures += "an ended process still counted as the requester"
            }
        } finally {
            foreach ($started in @($headless, $outside)) {
                if ($started -and -not $started.HasExited) {
                    Stop-Process -Id $started.Id -Force -ErrorAction SilentlyContinue
                    [void](Wait-PatchyProcessExit -ProcessId $started.Id -TimeoutSeconds 10)
                }
            }
            Remove-Item -LiteralPath $standInExe -Force -ErrorAction SilentlyContinue
            [IO.File]::WriteAllText($standInExe, "stand-in")
        }

        foreach ($language in $PatchyInstallerText.Keys) {
            $text = $PatchyInstallerText[$language]["LockedFileRetryMessage"]
            if (-not $text -or -not $text.Contains("{0}")) {
                $failures += "language $language has no usable locked-file message"
            }
        }
    } catch {
        $failures += $_.Exception.Message
    }
    if ($failures.Count -gt 0) {
        Write-Host ("Installer update-mode check FAILED: " + ($failures -join "; "))
        exit 1
    }
    Write-Host "Installer update-mode check passed."
    exit 0
}

if ($SmokeTest) {
    # Handled before the quiet branch below so a smoke test can never install, and
    # without the message box the real error path shows (it would wait for a click).
    if (-not [Environment]::UserInteractive) {
        Write-Host "Installer wizard smoke test needs an interactive desktop session."
        exit 3
    }
    try {
        $smoke = Show-PatchyInstallerWizard `
            -PayloadZip $PayloadZip `
            -InstallRoot $installRoot `
            -StartMenuDirectory $startMenuDirectory `
            -StartMenuShortcut $startMenuShortcut `
            -DesktopShortcut $desktopShortcut `
            -UninstallKey $uninstallKey `
            -Version $Version `
            -SmokeTest
        if ($smoke.Completed) { throw "The smoke test must not install." }
        Write-Host "Installer wizard smoke test passed."
        exit 0
    } catch {
        Write-Host "Installer wizard smoke test FAILED: $($_.Exception.Message)"
        exit 1
    }
}

try {
    if ($Update) {
        # Problems still ask (a Retry box) when there is a desktop to ask on; the install
        # itself needs no answers.
        $interactive = [Environment]::UserInteractive -and -not $Quiet
        # AI apps whose connector keeps running the previous version (its files are set
        # aside, not stopped); the updated Patchy tells the user to restart them.
        $connectorClients = @()
        if ($WaitForProcessId -gt 0 -and ($UpdateWorker -or -not $interactive)) {
            if (-not (Wait-PatchyProcessExit -ProcessId $WaitForProcessId)) {
                # Still there with no window: a Patchy that accepted the update before its
                # event loop started (see the function). One with a window keeps the Retry
                # box below.
                [void](Stop-PatchyHeadlessUpdateRequester -ProcessId $WaitForProcessId -InstallRoot $installRoot)
            }
        }
        if ($interactive -and -not $UpdateWorker) {
            # The install blocks its thread for many seconds, so it runs in a second copy
            # of this script while this one keeps the progress window alive.
            $progressForm = Show-PatchyUpdateProgress -Version $Version `
                -IconPath (Join-Path (Split-Path -Parent $PayloadZip) "Patchy.ico")
            $connectorClients = @(Get-PatchyConnectorClientNames -InstallRoot $installRoot)
            $workerExitCode = 1
            try {
                $workerInfo = New-Object System.Diagnostics.ProcessStartInfo
                $workerInfo.FileName = (Get-Process -Id $PID).Path
                $workerInfo.Arguments = "-NoLogo -NoProfile -ExecutionPolicy Bypass -STA -File `"$PSCommandPath`" " +
                    "-PayloadZip `"$PayloadZip`" -Version `"$Version`" -Update -UpdateWorker " +
                    "-InstallDir `"$installRoot`" -WaitForProcessId $WaitForProcessId"
                $workerInfo.UseShellExecute = $false
                $workerInfo.CreateNoWindow = $true
                $worker = [System.Diagnostics.Process]::Start($workerInfo)
                while (-not $worker.WaitForExit(30)) {
                    [System.Windows.Forms.Application]::DoEvents()
                }
                $workerExitCode = $worker.ExitCode
                $worker.Dispose()
                if ($workerExitCode -eq 0) {
                    # While the progress window is still up, so Patchy can come to the front.
                    if ($connectorClients.Count -gt 0) {
                        $env:PATCHY_UPDATE_CONNECTOR_CLIENTS = $connectorClients -join "|"
                    }
                    Start-PatchyInForeground -InstalledExe (Join-Path $installRoot "patchy.exe")
                }
            } finally {
                $progressForm.Close()
                $progressForm.Dispose()
            }
            exit $workerExitCode  # on a failure the worker already said why
        } else {
            if (-not $UpdateWorker) {
                $connectorClients = @(Get-PatchyConnectorClientNames -InstallRoot $installRoot)
            }
            $installedExe = Invoke-PatchyInstallWithRetry `
                -PayloadZip $PayloadZip `
                -InstallRoot $installRoot `
                -StartMenuDirectory $startMenuDirectory `
                -StartMenuShortcut $startMenuShortcut `
                -DesktopShortcut $desktopShortcut `
                -CreateDesktopShortcut (Test-Path -LiteralPath $desktopShortcut -PathType Leaf) `
                -UninstallKey $uninstallKey `
                -Version $Version `
                -Quiet (-not $interactive)
            Write-Host "Patchy updated in $(Split-Path -Parent $installedExe)"
            if ($UpdateWorker) {
                exit 0
            }
        }
        if ($connectorClients.Count -gt 0) {
            $env:PATCHY_UPDATE_CONNECTOR_CLIENTS = $connectorClients -join "|"
        }
        Start-PatchyInForeground -InstalledExe $installedExe
        exit 0
    }

    if ($Quiet -or -not [Environment]::UserInteractive) {
        $installedExe = Invoke-PatchyInstallWithRetry `
            -PayloadZip $PayloadZip `
            -InstallRoot $installRoot `
            -StartMenuDirectory $startMenuDirectory `
            -StartMenuShortcut $startMenuShortcut `
            -DesktopShortcut $desktopShortcut `
            -CreateDesktopShortcut $true `
            -UninstallKey $uninstallKey `
            -Version $Version `
            -Quiet $true
        Write-Host "Patchy installed to $(Split-Path -Parent $installedExe)"
        exit 0
    }

    $result = Show-PatchyInstallerWizard `
        -PayloadZip $PayloadZip `
        -InstallRoot $installRoot `
        -StartMenuDirectory $startMenuDirectory `
        -StartMenuShortcut $startMenuShortcut `
        -DesktopShortcut $desktopShortcut `
        -UninstallKey $uninstallKey `
        -Version $Version

    if ($result.Completed) {
        $installedRoot = Split-Path -Parent $result.InstalledExe
        Write-Host "Patchy installed to $installedRoot"
        if ($result.Launch -and -not $result.Launched -and (Test-Path -LiteralPath $result.InstalledExe -PathType Leaf)) {
            Start-PatchyInForeground -InstalledExe $result.InstalledExe
        }
    }

    exit 0
} catch {
    if ([Environment]::UserInteractive) {
        try {
            Add-Type -AssemblyName System.Windows.Forms
            [System.Windows.Forms.MessageBox]::Show(
                $_.Exception.Message,
                "Patchy Setup",
                [System.Windows.Forms.MessageBoxButtons]::OK,
                [System.Windows.Forms.MessageBoxIcon]::Error
            ) | Out-Null
        } catch {
        }
    }
    Write-Error $_
    exit 1
}

param(
    [ValidateSet("Daily", "Full")]
    [string]$Mode = "Daily"
)

$ErrorActionPreference = "Stop"

$projectPath = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $projectPath "build-output\DatabaseBackupCLI\DatabaseBackupCLI.exe"
$logPath = "D:\DatabaseBackups\scheduler.log"
$build2Bin = "D:\build2\build2-toolchain-0.18.1-x86_64-windows10-mingw-gcc14\bin"

$requiredVariables = @(
    "DB_HOST",
    "DB_PORT",
    "DB_NAME",
    "DB_USER",
    "DB_PASSWORD",
    "MYSQL_BACKUP_DATABASE",
    "MONGO_BACKUP_DATABASE"
)

function Write-BackupLog {
    param([string]$Message)

    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    Add-Content -Path $logPath -Value "[$timestamp] $Message"
}

function Invoke-BackupCommand {
    param(
        [string]$Label,
        [string[]]$CliArguments
    )

    Write-BackupLog "$Label started"

    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"

    try {
        $output = & $exePath @CliArguments 2>&1
        $commandExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }

    foreach ($line in $output) {
        Write-BackupLog ([string]$line)
    }

    Write-BackupLog "$Label finished with exit code $commandExitCode"
    return [int]$commandExitCode
}

Set-Location $projectPath
$env:COMPOSE_PROJECT_NAME = "databasebackupcli"

$env:Path = "D:\odb-install\bin;D:\odb-build\odb-mingw\libodb-pgsql-2.6.0\odb\pgsql;D:\odb-build\odb-mingw\libpq-14.1.0+7\libpq;D:\odb-build\odb-mingw\libcrypto-3.3.1\libcrypto;D:\odb-build\odb-mingw\libssl-3.3.1\libssl;D:\odb-build\odb-mingw\libz-1.3.100\libz;D:\odb-build\odb-mingw\libzstd-1.5.5+1\libzstd;$build2Bin;$env:Path"

if (-not (Test-Path $exePath)) {
    throw "CLI executable not found: $exePath"
}

if (-not (Test-Path "D:\DatabaseBackups")) {
    throw "Backup directory not found: D:\DatabaseBackups"
}

foreach ($name in $requiredVariables) {
    $value = [Environment]::GetEnvironmentVariable($name, "User")

    if ([string]::IsNullOrWhiteSpace($value)) {
        Write-BackupLog "Missing user environment variable: $name"
        throw "Missing user environment variable: $name"
    }

    Set-Item -Path "Env:$name" -Value $value
}

Write-BackupLog "Backup run started in mode $Mode"

$failedCommands = @()

if ($Mode -eq "Daily") {
    $exitCode = Invoke-BackupCommand `
        -Label "Backup-all" `
        -CliArguments @("backup-all")

    if ($exitCode -ne 0) {
        $failedCommands += "backup-all"
    }

    $exitCode = Invoke-BackupCommand `
        -Label "PostgreSQL cluster incremental backup" `
        -CliArguments @("backup-cluster", "incremental")

    if ($exitCode -ne 0) {
        $failedCommands += "cluster-incremental"
    }
}
else {
    $exitCode = Invoke-BackupCommand `
        -Label "PostgreSQL cluster full backup" `
        -CliArguments @("backup-cluster", "full")

    if ($exitCode -ne 0) {
        $failedCommands += "cluster-full"
    }
}

if ($failedCommands.Count -eq 0) {
    $exitCode = Invoke-BackupCommand `
        -Label "PostgreSQL cluster retention (keep 4 full chains)" `
        -CliArguments @("prune-cluster", "4", "--apply")

    if ($exitCode -ne 0) {
        $failedCommands += "cluster-retention"
    }
}

$backupExitCode = if ($failedCommands.Count -eq 0) { 0 } else { 1 }

if ($backupExitCode -eq 0) {
    if ($Mode -eq "Daily") {
        $notificationText = "DatabaseBackupCLI: backup jobs та PostgreSQL incremental cluster backup завершилися успішно."
    }
    else {
        $notificationText = "DatabaseBackupCLI: PostgreSQL full cluster backup завершився успішно."
    }

    Write-BackupLog "Backup run completed successfully"
}
else {
    $failedList = $failedCommands -join ", "
    $notificationText = "DatabaseBackupCLI: помилка резервного копіювання: $failedList."
    Write-BackupLog "Backup run failed: $failedList"
}

$slackWebhookUrl = [Environment]::GetEnvironmentVariable(
    "SLACK_WEBHOOK_URL",
    "User"
)

if ([string]::IsNullOrWhiteSpace($slackWebhookUrl)) {
    Write-BackupLog "Slack notification skipped: SLACK_WEBHOOK_URL is not set"
}
else {
    $payload = @{ text = $notificationText } | ConvertTo-Json -Compress

    try {
        Invoke-RestMethod `
            -Uri $slackWebhookUrl `
            -Method Post `
            -ContentType "application/json; charset=utf-8" `
            -Body $payload | Out-Null

        Write-BackupLog "Slack notification sent"
    }
    catch {
        Write-BackupLog "Slack notification failed: $($_.Exception.Message)"
    }
}

Write-BackupLog "Backup run finished with exit code $backupExitCode"

if ($backupExitCode -ne 0) {
    throw "Backup run failed. Check $logPath"
}
# Pico2 BME280 Monitor — Windows フォームセットアップ(Windows Forms, 追加依存なし)
#
# MSI で Program Files に展開されたバンドルのルートに setup.ps1 として配置される。
# 起動すると設定フォーム(DB ホスト/ポート/ユーザー/パスワード、HTTP ホスト/ポート、
# ログイン時自動起動)を表示し、「インストール」で scripts\install.ps1 に
# フォーム値を渡して実行する。進捗は同じウィンドウのログ欄に流れる。
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

[System.Windows.Forms.Application]::EnableVisualStyles()

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Pico2 BME280 Monitor セットアップ'
$form.ClientSize = New-Object System.Drawing.Size(560, 560)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false

function Add-Row($label, $default, $y, [switch]$Secure) {
    $l = New-Object System.Windows.Forms.Label
    $l.Text = $label; $l.Location = New-Object System.Drawing.Point(24, ($y + 4))
    $l.Size = New-Object System.Drawing.Size(180, 20)
    $t = if ($Secure) { New-Object System.Windows.Forms.TextBox } else { New-Object System.Windows.Forms.TextBox }
    if ($Secure) { $t.UseSystemPasswordChar = $true }
    $t.Text = $default; $t.Location = New-Object System.Drawing.Point(210, $y)
    $t.Size = New-Object System.Drawing.Size(320, 24)
    $form.Controls.Add($l); $form.Controls.Add($t)
    return $t
}

$title = New-Object System.Windows.Forms.Label
$title.Text = '測定環境の設定(空欄は既定値)'
$title.Font = New-Object System.Drawing.Font('Segoe UI', 11, [System.Drawing.FontStyle]::Bold)
$title.Location = New-Object System.Drawing.Point(24, 16)
$title.Size = New-Object System.Drawing.Size(500, 24)
$form.Controls.Add($title)

$pgHost     = Add-Row 'PostgreSQL ホスト'     '127.0.0.1'    52
$pgPort     = Add-Row 'PostgreSQL ポート'     '5433'         84
$pgDatabase = Add-Row 'データベース名'        'pico_monitor' 116
$pgUser     = Add-Row 'データベースユーザー'  'pico'         148
$pgPassword = Add-Row 'データベースパスワード' ''            180 -Secure
$httpHost   = Add-Row 'HTTP ホスト'           '127.0.0.1'    212
$httpPort   = Add-Row 'HTTP ポート'           '8787'         244

$service = New-Object System.Windows.Forms.CheckBox
$service.Text = 'ログイン時から自動起動(Pico を挿せば即測定)'
$service.Location = New-Object System.Drawing.Point(24, 282)
$service.Size = New-Object System.Drawing.Size(500, 24)
$service.Checked = $true
$form.Controls.Add($service)

$log = New-Object System.Windows.Forms.TextBox
$log.Multiline = $true; $log.ReadOnly = $true
$log.ScrollBars = 'Vertical'
$log.Font = New-Object System.Drawing.Font('Consolas', 9)
$log.Location = New-Object System.Drawing.Point(24, 340)
$log.Size = New-Object System.Drawing.Size(512, 196)
$form.Controls.Add($log)

$button = New-Object System.Windows.Forms.Button
$button.Text = 'インストール'
$button.Location = New-Object System.Drawing.Point(24, 300)
$button.Size = New-Object System.Drawing.Size(160, 32)
$form.Controls.Add($button)

function Append-Log($text) {
    $log.AppendText($text)
    $log.SelectionStart = $log.TextLength
    $log.ScrollToCaret()
    [System.Windows.Forms.Application]::DoEvents()
}

$button.Add_Click({
    $button.Enabled = $false
    $repoRoot = $PSScriptRoot

    Append-Log "セットアップを実行中(ダウンロードのため数分かかることがあります)...`n"
    $installArgs = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $repoRoot 'scripts\install.ps1'),
        '-PgHost', $pgHost.Text, '-PgPort', $pgPort.Text,
        '-PgDatabase', $pgDatabase.Text, '-PgUser', $pgUser.Text,
        '-PgPassword', $pgPassword.Text,
        '-HttpHost', $httpHost.Text, '-HttpPort', $httpPort.Text
    )
    if ($service.Checked) { $installArgs += '-Service' }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'powershell.exe'
    $psi.Arguments = ($installArgs | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
    $psi.WorkingDirectory = $repoRoot
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true

    try {
        $p = [System.Diagnostics.Process]::Start($psi)
        $outHandler = Register-ObjectEvent -InputObject $p -EventName OutputDataReceived -Action {
            if ($EventArgs.Data) { Append-Log ($EventArgs.Data + "`n") }
        }
        $errHandler = Register-ObjectEvent -InputObject $p -EventName ErrorDataReceived -Action {
            if ($EventArgs.Data) { Append-Log ($EventArgs.Data + "`n") }
        }
        $p.BeginOutputReadLine(); $p.BeginErrorReadLine()
        while (-not $p.WaitForExit(100)) { [System.Windows.Forms.Application]::DoEvents() }
        Unregister-Event -SourceIdentifier $outHandler.Name
        Unregister-Event -SourceIdentifier $errHandler.Name

        if ($p.ExitCode -eq 0) {
            Append-Log "`n完了!"
            [System.Windows.Forms.MessageBox]::Show(
                "http://$($httpHost.Text):$($httpPort.Text) をブラウザで開いてください。",
                'インストール完了') | Out-Null
        } else {
            Append-Log "`nエラー: セットアップが失敗しました(exit $($p.ExitCode))。ログを確認してください。"
            [System.Windows.Forms.MessageBox]::Show('セットアップに失敗しました。ログを確認してください。',
                'エラー') | Out-Null
        }
    } catch {
        Append-Log "`nエラー: $($_.Exception.Message)"
    }
    $button.Enabled = $true
})

[void]$form.ShowDialog()

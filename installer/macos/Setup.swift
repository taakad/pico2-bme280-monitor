// Pico2 BME280 Monitor — macOS フォームインストーラー (AppKit, 依存なし)。
//
// ビルドは scripts/package.sh が行う:
//   1) swiftc でこのファイルをコンパイルし Setup.app の実行ファイルに
//   2) 配布バンドル一式を Setup.app/Contents/Resources/bundle.tar.gz として同梱
//
// 起動すると設定フォーム(DB ホスト/ポート/ユーザー/パスワード、HTTP ホスト/ポート、
// ログイン時自動起動)を表示し、「インストール」で
//   ~/pico2-bme280-monitor にバンドルを展開 → scripts/install.sh にフォーム値を渡して実行
// を行う。進捗はログウィンドウに流れる。

import AppKit

final class InstallerWindow: NSWindow {
    private let pgHost = InstallerWindow.field("/tmp")
    private let pgPort = InstallerWindow.field("5433")
    private let pgDatabase = InstallerWindow.field("pico_monitor")
    private let pgUser = InstallerWindow.field("pico")
    private let pgPassword = InstallerWindow.secureField("")
    private let httpHost = InstallerWindow.field("127.0.0.1")
    private let httpPort = InstallerWindow.field("8787")
    private let serviceCheck = NSButton(checkboxWithTitle: "ログイン時から自動起動(Pico を挿せば即測定)",
                                        target: nil, action: nil)
    private let log = NSTextView()
    private let installButton = NSButton(title: "インストール", target: nil, action: nil)
    private var running = false

    init() {
        super.init(contentRect: NSRect(x: 0, y: 0, width: 560, height: 620),
                   styleMask: [.titled, .closable, .miniaturizable],
                   backing: .buffered, defer: false)
        title = "Pico2 BME280 Monitor セットアップ"
        isReleasedWhenClosed = false

        let content = NSView(frame: contentLayoutRect)
        contentView = content

        var y: CGFloat = content.bounds.height - 40
        func addRow(_ label: String, _ control: NSView) {
            let l = InstallerWindow.labelFor(label)
            l.frame = NSRect(x: 24, y: y, width: 180, height: 22)
            control.frame = NSRect(x: 210, y: y, width: 320, height: 24)
            content.addSubview(l)
            content.addSubview(control)
            y -= 36
        }

        let heading = InstallerWindow.labelFor("測定環境の設定(空欄は既定値)")
        heading.font = NSFont.boldSystemFont(ofSize: 14)
        heading.frame = NSRect(x: 24, y: y, width: 500, height: 24)
        content.addSubview(heading)
        y -= 34

        addRow("PostgreSQL ホスト", pgHost)
        addRow("PostgreSQL ポート", pgPort)
        addRow("データベース名", pgDatabase)
        addRow("データベースユーザー", pgUser)
        addRow("データベースパスワード", pgPassword)
        addRow("HTTP ホスト", httpHost)
        addRow("HTTP ポート", httpPort)

        serviceCheck.frame = NSRect(x: 24, y: y, width: 500, height: 22)
        serviceCheck.state = .on
        content.addSubview(serviceCheck)
        y -= 34

        installButton.frame = NSRect(x: 24, y: y, width: 160, height: 32)
        installButton.target = self
        installButton.action = #selector(install)
        content.addSubview(installButton)
        y -= 46

        let scroll = NSScrollView(frame: NSRect(x: 24, y: 20,
                                                width: content.bounds.width - 48, height: y - 20))
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        log.isEditable = false
        log.font = NSFont.monospacedSystemFont(ofSize: 11, weight: .regular)
        log.autoresizingMask = [.height]
        scroll.documentView = log
        content.addSubview(scroll)

        center()
    }

    private static func field(_ value: String) -> NSTextField {
        NSTextField(string: value)
    }
    private static func secureField(_ value: String) -> NSSecureTextField {
        NSSecureTextField(string: value)
    }
    private static func labelFor(_ text: String) -> NSTextField {
        NSTextField(labelWithString: text)
    }

    private func append(_ text: String) {
        log.string += text
        log.scrollToEndOfDocument(nil)
    }

    @objc private func install() {
        guard !running else { return }
        running = true
        installButton.isEnabled = false

        let values: [String: String] = [
            "pgHost": pgHost.stringValue, "pgPort": pgPort.stringValue,
            "pgDatabase": pgDatabase.stringValue, "pgUser": pgUser.stringValue,
            "pgPassword": pgPassword.stringValue,
            "httpHost": httpHost.stringValue, "httpPort": httpPort.stringValue,
        ]
        let service = serviceCheck.state == .on

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            self?.runInstall(values: values, service: service)
            DispatchQueue.main.async {
                self?.running = false
                self?.installButton.isEnabled = true
            }
        }
    }

    private func runInstall(values: [String: String], service: Bool) {
        let fm = FileManager.default
        guard let resource = Bundle.main.resourceURL else { return fail("リソースが見つかりません") }
        let tarball = resource.appendingPathComponent("bundle.tar.gz")
        guard fm.fileExists(atPath: tarball.path) else { return fail("bundle.tar.gz がありません") }
        let home = fm.homeDirectoryForCurrentUser
        let dest = home.appendingPathComponent("pico2-bme280-monitor")

        append("バンドルを展開中: \(dest.path)\n")
        if fm.fileExists(atPath: dest.path) { try? fm.removeItem(at: dest) }
        try? fm.createDirectory(at: dest, withIntermediateDirectories: true)

        // bsdtar の --strip-components でトップレベルフォルダーを潰して展開
        let tar = Process()
        tar.executableURL = URL(fileURLWithPath: "/usr/bin/tar")
        tar.arguments = ["-xzf", tarball.path, "-C", dest.path, "--strip-components=1"]
        if !run(tar, pipeToUI: true) { return fail("展開に失敗しました") }

        var args = ["scripts/install.sh"]
        if service { args.append("--service") }
        args += ["--pg-host", values["pgHost"] ?? "", "--pg-port", values["pgPort"] ?? "",
                 "--pg-database", values["pgDatabase"] ?? "", "--pg-user", values["pgUser"] ?? "",
                 "--pg-password", values["pgPassword"] ?? "",
                 "--http-host", values["httpHost"] ?? "", "--http-port", values["httpPort"] ?? ""]

        append("セットアップを実行中(ダウンロードのため数分かかることがあります)...\n\n")
        let install = Process()
        install.executableURL = URL(fileURLWithPath: "/bin/bash")
        install.arguments = args
        install.currentDirectoryURL = dest
        if !run(install, pipeToUI: true) { return fail("install.sh が失敗しました。ログを確認してください。") }

        append("\n完了! ブラウザで http://\(values["httpHost"] ?? "127.0.0.1"):\(values["httpPort"] ?? "8787") を開いてください。\n")
        let alert = NSAlert()
        alert.messageText = "インストール完了"
        alert.informativeText = "http://\(values["httpHost"] ?? "127.0.0.1"):\(values["httpPort"] ?? "8787") をブラウザで開いてください。"
        DispatchQueue.main.async { alert.runModal() }
    }

    private func run(_ process: Process, pipeToUI: Bool) -> Bool {
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe
        pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            guard !data.isEmpty, let text = String(data: data, encoding: .utf8) else { return }
            DispatchQueue.main.async { self?.append(text) }
        }
        do { try process.run() } catch { append("起動失敗: \(error)\n"); return false }
        process.waitUntilExit()
        pipe.fileHandleForReading.readabilityHandler = nil
        return process.terminationStatus == 0
    }

    private func fail(_ message: String) {
        append("\nエラー: \(message)\n")
        let alert = NSAlert()
        alert.messageText = "セットアップに失敗しました"
        alert.informativeText = message
        DispatchQueue.main.async { alert.runModal() }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    var window: InstallerWindow?
    func applicationDidFinishLaunching(_ notification: Notification) {
        let w = InstallerWindow()
        w.makeKeyAndOrderFront(nil)
        window = w
        NSApp.activate(ignoringOtherApps: true)
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.regular)
app.run()

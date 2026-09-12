use tauri::{
    tray::TrayIconBuilder,
    menu::{Menu, MenuItem},
    Manager, WindowEvent,
};
use tauri_plugin_shell::{ShellExt, process::CommandChild};
use std::sync::Mutex;

// holds the daemon child process so we can kill it explicitly on real quit
struct DaemonHandle(Mutex<Option<CommandChild>>);

#[tauri::command]
fn greet(name: &str) -> String {
    format!("Hello, {}! You've been greeted from Rust!", name)
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_shell::init())
        .manage(DaemonHandle(Mutex::new(None)))
        .invoke_handler(tauri::generate_handler![greet])
        .setup(|app| {
            // start the daemon sidecar once, on launch. not tied to the
            // window's lifetime, keeps running even if the window is hidden
            let sidecar = app.shell().sidecar("pulsedb_daemon")
                .expect("failed to find pulsedb_daemon sidecar")
                .spawn()
                .expect("failed to spawn pulsedb_daemon");
            let (_rx, child) = sidecar;
            *app.state::<DaemonHandle>().0.lock().unwrap() = Some(child);

            // tray icon with show/quit, quit is the only thing that actually
            // kills the daemon, closing the window just hides it
            let show_item = MenuItem::with_id(app, "show", "Show PulseDB", true, None::<&str>)?;
            let quit_item = MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?;
            let menu = Menu::with_items(app, &[&show_item, &quit_item])?;

            TrayIconBuilder::new()
                .icon(app.default_window_icon().unwrap().clone())
                .menu(&menu)
                .on_menu_event(|app, event| match event.id.as_ref() {
                    "show" => {
                        if let Some(window) = app.get_webview_window("main") {
                            let _ = window.show();
                            let _ = window.set_focus();
                        }
                    }
                    "quit" => {
                        if let Some(child) = app.state::<DaemonHandle>().0.lock().unwrap().take() {
                            let _ = child.kill();
                        }
                        app.exit(0);
                    }
                    _ => {}
                })
                .build(app)?;

            Ok(())
        })
        .on_window_event(|window, event| {
            // closing the window hides it instead of quitting the app,
            // the tray icon is what actually stays running
            if let WindowEvent::CloseRequested { api, .. } = event {
                window.hide().unwrap();
                api.prevent_close();
            }
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
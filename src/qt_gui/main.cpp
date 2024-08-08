// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QtWidgets/QApplication>
#include <fmt/core.h>

#include "common/config.h"
#include "core/file_sys/fs.h"
#include "emulator.h"
#include "qt_gui/game_install_dialog.h"
#include "qt_gui/main_window.h"

// Custom message handler to ignore Qt logs
void customMessageHandler(QtMsgType, const QMessageLogContext&, const QString&) {}

int main(int argc, char* argv[]) {
    QApplication a(argc, argv);

    // Load configurations and initialize Qt application
    const auto user_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    Config::load(user_dir / "config.toml");
    std::filesystem::create_directory(user_dir / "game_data");

    // Check if elf or eboot.bin path was passed as a command line argument
    bool has_command_line_argument = argc > 1;

    // Check if the game install directory is set
    if (Config::getGameInstallDir() == "" && !has_command_line_argument) {
        GameInstallDialog dlg;
        dlg.exec();
    }

    // Ignore Qt logs
    qInstallMessageHandler(customMessageHandler);

    // Initialize the main window
    MainWindow* m_main_window = new MainWindow(nullptr);
    m_main_window->Init();

    // Check for command line arguments
    if (has_command_line_argument) {
        Core::Emulator emulator;
        emulator.Run(argv[1]);
    }

    // Run the Qt application
    return a.exec();
}

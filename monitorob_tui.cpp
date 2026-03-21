#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <filesystem>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>

using namespace std;
using namespace ftxui;

const char* SOCKET_PATH = "/tmp/monitorob.sock";

// --- データ構造と通信用関数 ---
struct ProcessInfo {
    string name;
    string status;
    bool auto_start;
    int pid;
    long uptime_sec;
    bool is_logging;
};

string send_command(const string& cmd) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) return "ERROR: Cannot connect to server";
    
    string request = cmd + "\n";
    send(sock, request.c_str(), request.length(), 0);
    char buffer[4096] = {0};
    ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);
    close(sock);
    return bytes_read > 0 ? string(buffer, bytes_read) : "ERROR";
}

vector<ProcessInfo> fetch_processes() {
    vector<ProcessInfo> list;
    string response = send_command("STATUS");
    if (response.find("ERROR") == 0 || response.find("EMPTY") == 0) return list;

    istringstream stream(response);
    string line;
    while (getline(stream, line)) {
        if (line.empty()) continue;
        istringstream linestream(line);
        string token;
        vector<string> cols;
        while (getline(linestream, token, '\t')) cols.push_back(token);

        if (cols.size() >= 6) {
            try {
                ProcessInfo info;
                info.name = cols[0];
                info.status = cols[1];
                info.auto_start = (cols[2] == "1");
                info.pid = stoi(cols[3]);
                info.uptime_sec = stol(cols[4]);
                info.is_logging = (cols[5] == "1");
                list.push_back(info);
            } catch (...) {}
        }
    }
    return list;
}

string format_uptime(long seconds) {
    if (seconds <= 0) return "-";
    long h = seconds / 3600, m = (seconds % 3600) / 60, s = seconds % 60;
    string res = "";
    if (h > 0) res += to_string(h) + "h ";
    if (m > 0) res += to_string(m) + "m ";
    res += to_string(s) + "s";
    return res;
}

// --- メイン関数 (FTXUI描画) ---
int main() {
    auto screen = ScreenInteractive::Fullscreen();

    vector<ProcessInfo> processes;
    int selected_index = 0;
    string status_message = "Starting monitorob TUI...";
    mutex data_mutex;
    atomic<bool> running = true;

    thread update_thread([&]() {
        while (running) {
            auto new_data = fetch_processes();
            {
                lock_guard<mutex> lock(data_mutex);
                processes = new_data;
                if (selected_index >= (int)processes.size()) selected_index = max(0, (int)processes.size() - 1);
            }
            screen.PostEvent(Event::Custom);
            this_thread::sleep_for(chrono::seconds(1));
        }
    });

    auto perform_action = [&](const string& cmd_prefix) {
        lock_guard<mutex> lock(data_mutex);
        if (selected_index >= 0 && selected_index < (int)processes.size()) {
            string target = processes[selected_index].name;
            string cmd;

            // ★ LOGSボタンのトグル切り替え処理
            if (cmd_prefix == "LOG_TOGGLE") {
                if (processes[selected_index].is_logging) {
                    cmd = "LOG_STOP " + target;
                } else {
                    cmd = "LOG_START " + target + " " + std::filesystem::current_path().string() + "/log";
                }
            } else {
                cmd = cmd_prefix + " " + target;
            }

            status_message = send_command(cmd);
            if (!status_message.empty() && status_message.back() == '\n') status_message.pop_back();
        }
    };

    // --- コンポーネント定義 ---
    
    auto btn_start = Button(" ▶ START ", [&]{ perform_action("START"); });
    auto btn_stop  = Button(" ■ STOP ", [&]{ perform_action("STOP"); });
    auto btn_logs  = Button(" 📝 LOGS ", [&]{ perform_action("LOG_TOGGLE"); }); // トグル機能に変更
    auto btn_rem   = Button(" 🗑 REMOVE ", [&]{ perform_action("REMOVE"); });
    auto action_buttons = Container::Horizontal({btn_start, btn_stop, btn_logs, btn_rem});

    string new_name, new_dir, new_cmd;
    InputOption option;
    auto input_name = Input(&new_name, "Name (e.g., app1)", option);
    auto input_dir  = Input(&new_dir, "Dir (e.g., /tmp)", option);
    auto input_cmd  = Input(&new_cmd, "Command (e.g., ./run)", option);

    auto btn_add = Button(" ✚ ADD ", [&]{
        if (!new_name.empty() && !new_dir.empty() && !new_cmd.empty()) {
            string cmd = "ADD " + new_name + " " + new_dir + " " + new_cmd;
            status_message = send_command(cmd);
            if (!status_message.empty() && status_message.back() == '\n') status_message.pop_back();
            new_name = ""; new_dir = ""; new_cmd = "";
        } else {
            status_message = "Error: All fields are required to ADD.";
        }
    });
    auto add_form = Container::Horizontal({input_name, input_dir, input_cmd, btn_add});

    auto main_container = Container::Vertical({action_buttons, add_form});

    // --- 画面レンダリング ---
    auto renderer = Renderer(main_container, [&] {
        lock_guard<mutex> lock(data_mutex);

        vector<vector<Element>> table_data;
        table_data.push_back({
            text(" NAME") | bold, text(" "),
            text("STATE") | bold, text(" "),
            text("AUTO") | bold,  text(" "), 
            text("PID") | bold,   text(" "),
            text("UPTIME") | bold, text(" "),
            text("LOGS") | bold
        });
        
        for (int i = 0; i < (int)processes.size(); ++i) {
            const auto& p = processes[i];
            
            auto e_name = text(" " + p.name);
            auto e_state = text(p.status) | color(p.status == "RUNNING" ? Color::GreenLight : Color::RedLight);
            auto e_auto = text(p.auto_start ? "[ON] " : "[OFF]") | color(p.auto_start ? Color::CyanLight : Color::GrayDark);
            auto e_pid = text(p.pid != -1 ? to_string(p.pid) : "-  "); 
            auto e_uptime = text(format_uptime(p.uptime_sec));
            auto e_logs = text(p.is_logging ? "[PIPE]" : "      ") | color(Color::YellowLight); 

            // ★ 選択行の配色を「背景青＋文字白」で固定して見やすくする（invertedの廃止）
            if (i == selected_index) {
                e_name   = text(" " + p.name)                         | bgcolor(Color::Blue) | color(Color::White) | bold;
                e_state  = text(p.status)                             | bgcolor(Color::Blue) | color(Color::White) | bold;
                e_auto   = text(p.auto_start ? "[ON] " : "[OFF]")     | bgcolor(Color::Blue) | color(Color::White);
                e_pid    = text(p.pid != -1 ? to_string(p.pid) : "-") | bgcolor(Color::Blue) | color(Color::White);
                e_uptime = text(format_uptime(p.uptime_sec))          | bgcolor(Color::Blue) | color(Color::White);
                e_logs   = text(p.is_logging ? "[PIPE]" : "      ")   | bgcolor(Color::Blue) | color(Color::White);
            }
            table_data.push_back({e_name, text(" "), e_state, text(" "), e_auto, text(" "), e_pid, text(" "), e_uptime, text(" "), e_logs});
        }

        auto table = Table(table_data);
        table.SelectAll().Separator(LIGHT); 

        // ★ 返答メッセージが長すぎる場合は切り詰める (最大50文字)
        string disp_msg = status_message;
        if (disp_msg.length() > 50) {
            disp_msg = disp_msg.substr(0, 47) + "...";
        }

        return vbox(Elements{
            hbox(Elements{ filler(), text(" monitorob Dashboard ") | bold | color(Color::White), filler() }) | bgcolor(Color::Blue),
            separator() | color(Color::GrayDark),
            table.Render() | flex, 
            separator() | color(Color::GrayDark),
            
            hbox(Elements{ 
                text(" Selected Actions: ") | bold, text(" "),
                btn_start->Render(), text(" "), 
                btn_stop->Render(), text(" "), 
                btn_logs->Render(), text(" "), 
                btn_rem->Render()
            }) | center,
            
            text(" ") | size(HEIGHT, EQUAL, 1), 
            
            // ★ Add New Process を2段組みにし、Cmdを1行まるまる使うように変更
            vbox(Elements{
                text(" Add New Process: ") | bold,
                // 上段: Name, Dir, Addボタン
                hbox(Elements{ 
                    vbox(Elements{ filler(), text("  Name: "), filler() }), 
                    input_name->Render() | border | size(WIDTH, EQUAL, 20), 
                    vbox(Elements{ filler(), text("  Dir: "), filler() }),  
                    input_dir->Render()  | border | flex, 
                    text("  "),
                    btn_add->Render() | border
                }),
                // 下段: Cmd入力欄 (横幅いっぱい)
                hbox(Elements{ 
                    vbox(Elements{ filler(), text("  Cmd:  "), filler() }),  
                    input_cmd->Render()  | border | flex
                })
            }),
            
            separator() | color(Color::GrayDark),
            
            hbox(Elements{
                text(" Daemon Reply: " + disp_msg) | color(Color::YellowLight),
                filler(),
                hbox(Elements{
                    text(" [↑/↓] Select Row |") | color(Color::GrayLight),
                    text(" [TAB] Move Focus |")  | color(Color::GrayLight),
                    text(" [Enter] Exec |")      | color(Color::White) | bold,
                    text(" [q] Quit ")            | color(Color::RedLight)
                })
            })
        }) | border;
    });

    renderer |= CatchEvent([&](Event event) {
        if (input_name->Focused() || input_dir->Focused() || input_cmd->Focused()) {
            return false; 
        }
        if (event == Event::Character('q') || event == Event::Character('Q')) {
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::ArrowUp || event == Event::Character('k')) {
            lock_guard<mutex> lock(data_mutex);
            if (selected_index > 0) selected_index--;
            return true;
        }
        if (event == Event::ArrowDown || event == Event::Character('j')) {
            lock_guard<mutex> lock(data_mutex);
            if (selected_index < (int)processes.size() - 1) selected_index++;
            return true;
        }
        return false;
    });

    screen.Loop(renderer);

    running = false;
    update_thread.join();

    return 0;
}

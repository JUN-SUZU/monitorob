#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <sstream>
#include <deque>
#include <csignal>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctime>

using namespace std;
namespace fs = std::filesystem;

// 管理対象プロセスの構造体
struct Process {
    string name;
    string work_dir;
    string command;
    bool should_run = false;
    pid_t pid = -1;
    
    // ログ関連
    int pipe_fd = -1;             // 読み取り用パイプ
    string partial_line;          // 改行待ちバッファ
    deque<string> log_buffer;     // 100行保持用リングバッファ
    bool is_logging = false;      // ファイル書き出し中フラグ
    ofstream log_file;            // ログファイルストリーム
    time_t log_start_time = 0;    // 10分タイマー用
    time_t last_start_time = 0;
    time_t stop_signaled_time = 0;
    time_t actual_start_time = 0;
};

unordered_map<string, Process> managed_processes;
mutex mtx;
const char* SOCKET_PATH = "/tmp/monitorob.sock";
const string CONFIG_FILE = "servers.conf";

// --- ユーティリティ ---
void sigchld_handler(int signo) {
    // 終了した子プロセスを全て回収（ゾンビ防止）
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

bool isProcessAlive(pid_t pid) {
    if (pid <= 0) return false;
    return kill(pid, 0) == 0;
}

// 設定の永続化
void saveConfig() {
    ofstream conf(CONFIG_FILE);
    for (const auto& [name, p] : managed_processes) {
        conf << "{{" << p.name << "}}{{" << p.work_dir << "}}{{" << p.command << "}}{{" << (p.should_run ? "1" : "0") << "}}\n";
    }
}

// 起動時の設定読み込み
void loadConfig() {
    ifstream conf(CONFIG_FILE);
    if (!conf) return;

    string line;
    while (getline(conf, line)) {
        if (line.empty()) continue;
        vector<string> elements;
        string temp = line;
        for (int i = 0; i < 4; i++) {
            size_t start = temp.find("{{");
            size_t end = temp.find("}}");
            if (start == string::npos || end == string::npos) break;
            elements.push_back(temp.substr(start + 2, end - start - 2));
            temp = temp.substr(end + 2);
        }
        if (elements.size() == 4) {
            string name = elements[0];
            Process& p = managed_processes[name]; 
            p.name = name;
            p.work_dir = elements[1];
            p.command = elements[2];
            p.should_run = (elements[3] == "1");
        }
    }
}

// --- スレッド1: ログ読み取り（非同期） ---
void log_reader_thread() {
    char buf[2048];
    while (true) {
        {
            lock_guard<mutex> lock(mtx);
            for (auto& [name, p] : managed_processes) {
                if (p.pipe_fd != -1) {
                    ssize_t n = read(p.pipe_fd, buf, sizeof(buf) - 1);
                    if (n > 0) {
                        buf[n] = '\0';
                        p.partial_line += buf;

                        size_t pos;
                        while ((pos = p.partial_line.find('\n')) != string::npos) {
                            string line = p.partial_line.substr(0, pos);
                            p.partial_line.erase(0, pos + 1);

                            p.log_buffer.push_back(line);
                            if (p.log_buffer.size() > 100) {
                                p.log_buffer.pop_front();
                            }

                            if (p.is_logging && p.log_file.is_open()) {
                                p.log_file << line << "\n";
                                p.log_file.flush();
                            }
                        }
                    } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                        close(p.pipe_fd);
                        p.pipe_fd = -1;
                    }
                }
            }
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

// --- スレッド2: TUIからのIPC通信処理 ---
void handle_client(int client_sock) {
    char buffer[4096] = {0};
    while (true) {
        ssize_t bytes_read = read(client_sock, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) break;
        
        buffer[bytes_read] = '\0';
        string input(buffer);
        istringstream iss(input);
        string cmd, name;
        iss >> cmd;

        string response;
        lock_guard<mutex> lock(mtx);

        if (cmd == "STATUS") {
            response = "";
            time_t now = time(NULL);
            for (const auto& [n, p] : managed_processes) {
                bool alive = isProcessAlive(p.pid);
                long uptime = alive ? (now - p.actual_start_time) : 0;
                
                // フォーマット: 名前[TAB]状態[TAB]自動起動[TAB]PID[TAB]稼働時間(秒)[TAB]ログ出力中
                response += n + "\t" + 
                            (alive ? "RUNNING" : "STOPPED") + "\t" +
                            (p.should_run ? "1" : "0") + "\t" +
                            to_string(alive ? p.pid : -1) + "\t" +
                            to_string(uptime) + "\t" +
                            (p.is_logging ? "1" : "0") + "\n";
            }
            if (response.empty()) response = "EMPTY\n";
        } 
        else if (cmd == "ADD") {
            string dir, pcmd;
            iss >> name >> dir;
            getline(iss, pcmd);
            if (!pcmd.empty() && pcmd[0] == ' ') pcmd.erase(0, 1);
            Process& p = managed_processes[name];
            p.name = name;
            p.work_dir = dir;
            p.command = pcmd;
            p.should_run = true;
            p.pid = -1;
            
            saveConfig();
            response = "Added: " + name + "\n";
        } 
        else if (cmd == "REMOVE") {
            iss >> name;
            if (managed_processes.count(name)) {
                if (isProcessAlive(managed_processes[name].pid)) {
                    kill(-managed_processes[name].pid, SIGTERM);
                }
                managed_processes.erase(name);
                saveConfig();
                response = "Removed: " + name + "\n";
            } else {
                response = "Not found.\n";
            }
        } 
        else if (cmd == "START" || cmd == "STOP") {
            iss >> name;
            if (managed_processes.count(name)) {
                managed_processes[name].should_run = (cmd == "START");
                saveConfig();
                response = cmd + " signaled for: " + name + "\n";
            } else {
                response = "Not found.\n";
            }
        }
        else if (cmd == "LOG_START") {
            string log_dir;
            iss >> name >> log_dir;
            if (managed_processes.count(name)) {
                Process& p = managed_processes[name];
                fs::create_directories(log_dir);
                string file_path = log_dir + "/" + name + ".log";
                
                p.log_file.open(file_path, ios::app);
                if (p.log_file.is_open()) {
                    p.is_logging = true;
                    p.log_start_time = time(NULL);
                    
                    p.log_file << "--- Log capture started ---\n";
                    for (const auto& line : p.log_buffer) p.log_file << line << "\n";
                    p.log_file.flush();
                    
                    response = "Logging to: " + file_path + "\n";
                } else {
                    response = "Error opening file.\n";
                }
            } else {
                response = "Not found.\n";
            }
        }
        else if (cmd == "LOG_STOP") {
            iss >> name;
            if (managed_processes.count(name)) {
                Process& p = managed_processes[name];
                p.is_logging = false;
                if (p.log_file.is_open()) p.log_file.close();
                response = "Logging stopped for " + name + ".\n";
            }
        }
        else {
            response = "Unknown command.\n";
        }
        send(client_sock, response.c_str(), response.length(), 0);
    }
    close(client_sock);
}

void ipc_server_thread() {
    int server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    unlink(SOCKET_PATH);
    bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_sock, 5);

    while (true) {
        int client_sock = accept(server_sock, NULL, NULL);
        if (client_sock >= 0) handle_client(client_sock);
    }
    close(server_sock);
}

// --- メインスレッド: 死活監視と調停 ---
int main() {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    loadConfig();

    thread ipc_thread(ipc_server_thread);
    ipc_thread.detach();

    thread log_thread(log_reader_thread);
    log_thread.detach();

    cout << "monitorob server started. Managed processes: " << managed_processes.size() << endl;

    while (true) {
        time_t current_time = time(NULL);
        {
            lock_guard<mutex> lock(mtx);

            for (auto& [name, p] : managed_processes) {
                // 10分タイマーチェック
                if (p.is_logging && (current_time - p.log_start_time >= 600)) {
                    p.is_logging = false;
                    if (p.log_file.is_open()) p.log_file.close();
                    cout << "[Monitor] Auto-stopped log piping for " << name << endl;
                }

                bool isAlive = isProcessAlive(p.pid);

                if (p.should_run){
                    p.stop_signaled_time = 0;
                    if(!isAlive) {
                        if (current_time - p.last_start_time < 3) continue;
                        p.last_start_time = current_time;

                        if (p.pipe_fd != -1) {
                            close(p.pipe_fd);
                            p.pipe_fd = -1;
                        }
                        p.partial_line = "";

                        int pipefd[2];
                        if (pipe(pipefd) < 0) {
                            perror("pipe failed");
                            continue; 
                        }

                        pid_t pid = fork();
                        if (pid == 0) { // 子プロセス
                            close(pipefd[0]);
                            dup2(pipefd[1], STDOUT_FILENO);
                            dup2(pipefd[1], STDERR_FILENO);
                            close(pipefd[1]);

                            fs::current_path(p.work_dir);
                            
                            // 引数のパース
                            vector<string> args;
                            istringstream arg_stream(p.command);
                            string token;
                            while (arg_stream >> token) args.push_back(token);
                            vector<char*> c_args;
                            for (auto& arg : args) c_args.push_back(const_cast<char*>(arg.c_str()));
                            c_args.push_back(nullptr);

                            setpgid(0, 0); // プロセスグループリーダー化
                            execvp(c_args[0], c_args.data());
                            _exit(127);
                        } else if (pid > 0) { // 親プロセス
                            close(pipefd[1]);
                            p.pid = pid;
                            p.pipe_fd = pipefd[0];
                            
                            // パイプをノンブロッキングに設定
                            int flags = fcntl(p.pipe_fd, F_GETFL, 0);
                            fcntl(p.pipe_fd, F_SETFL, flags | O_NONBLOCK);
                            p.actual_start_time = current_time;
                            cout << "[Monitor] Started " << name << " (PID: " << pid << ")" << endl;
                        }
                    }
                }
                else {
                    if (isAlive) {
                        if (p.stop_signaled_time == 0) {
                            // 初回の停止要求 (SIGTERM)
                            cout << "[Monitor] Stopping " << name << " (Sending SIGTERM)" << endl;
                            kill(-p.pid, SIGTERM);
                            p.stop_signaled_time = current_time;
                        } 
                        else if (current_time - p.stop_signaled_time >= 5) {
                            // 5秒経っても死んでいないなら強制終了 (SIGKILL)
                            cout << "[Monitor] Force killing " << name << " (Sending SIGKILL)" << endl;
                            kill(-p.pid, SIGKILL);
                            p.stop_signaled_time = current_time; 
                        }
                    } 
                    else {
                        // 完全に死んでいるのを確認できたタイミングでPIDをリセット
                        if (p.pid != -1) {
                            p.pid = -1;
                            p.stop_signaled_time = 0;
                            cout << "[Monitor] Process " << name << " is completely stopped." << endl;
                        }
                    }
                }
            }
        }
        this_thread::sleep_for(chrono::seconds(3)); // 3秒周期で監視
    }
    return 0;
}

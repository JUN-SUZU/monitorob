#include <iostream>
#include <fstream>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <stdlib.h>
#include <string>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <vector>
using namespace std;

void sigchld_handler(int signo)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

bool getAlive(pid_t pid)
{
    return kill(pid, 0) == 0;
}

double GetCPUUsage() {
    static long prevTotalTime = 0, prevProcTime = 0;
    long procTime, totalTime;
    char buf[256];
    std::ifstream stat("/proc/self/stat");
    if (!stat) return -1;

    // プロセスのCPU時間を取得
    stat.getline(buf, sizeof(buf));
    char* ptr = strtok(buf, " "); // 1列目を飛ばす
    for (int i = 0; i < 13; i++) ptr = strtok(nullptr, " ");
    procTime = std::stol(ptr) + std::stol(strtok(nullptr, " ")); // utime + stime

    // システム全体のCPU時間を取得
    std::ifstream statTotal("/proc/stat");
    if (!statTotal) return -1;

    totalTime = 0;
    statTotal.getline(buf, sizeof(buf));
    ptr = strtok(buf, " "); // "cpu" を飛ばす
    while ((ptr = strtok(nullptr, " "))) totalTime += std::stol(ptr);

    // 使用率計算
    double usage = (procTime - prevProcTime) * 100.0 / (totalTime - prevTotalTime);
    prevProcTime = procTime;
    prevTotalTime = totalTime;

    return usage;
}

size_t GetMemoryUsage() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss; // KB単位
    }
    return 0;
}

vector<vector<string>> readConfig()
{
    // サーバーの設定ファイルを読み込む
    // 書式
    // 1行に1つのサーバーの設定
    // 要素はスペース区切り 波かっこ2つで囲む例: {{}} {{}}
    // 空行を間に挟むとエラーになる
    // 1列目: サーバーの名前
    // 2列目: サーバーのルートディレクトリの絶対パス
    // 3列目: サーバーの起動コマンド(コマンドは/bin不要, ファイル名はフルパス)
    // 4列目: 設定したいサーバーの状態(0: offline, 1: online)(0でオンラインになっていたら停止する, 1でオフラインになっていたら起動する)
    // 例
    // {{server1}} {{/var/www/html}} {{node /var/www/html/index.js}} {{0}}
    // {{server2}} {{/home/user/projects/app}} {{node /home/jun/projects/app/index.js}} {{1}}
    vector<vector<string>> servers;
    ifstream conf("servers.conf");
    if (!conf)
    {
        cerr << "Failed to open servers.conf" << endl;
        exit(1);
    }
    string line;
    while (getline(conf, line))
    {
        if (line.empty())
            continue; // 空行ならスキップ
        vector<string> server;
        string element;
        for (int i = 0; i < 4; i++)
        {
            size_t start = line.find("{{");
            size_t end = line.find("}}");
            element = line.substr(start + 2, end - start - 2);
            line = line.substr(end + 2);
            server.push_back(element);
        }
        servers.push_back(server);
    }
    conf.close(); // ファイルを閉じる
    return servers;
}

int main()
{
    // サーバーの設定ファイルを読み込む
    vector<vector<string>> servers;
    servers = readConfig();
    int P_MAX = servers.size();
    int status[P_MAX];
    int pid[P_MAX];

    // ゾンビプロセスを回避するためのシグナルハンドラ
    signal(SIGCHLD, sigchld_handler);

    // 子プロセスを生成してサーバーを起動する
    int i;
    for (i = 0; i < P_MAX && (pid[i] = fork()) > 0; i++)
        ;
    cout << "Process started with PID: " << pid[i] << endl;

    if (i == P_MAX) // 親プロセスはすべての子プロセスの終了を待つ
    {
        while (true)
        {
            servers = readConfig();
            P_MAX = servers.size();
            for (int j = 0; j < P_MAX; j++)
            {
                bool isAlive = getAlive(pid[j]);
                if (isAlive && servers[j][3] == "0") // サーバーがオンラインで設定がオフラインなら停止する
                {
                    cout << "Stopping server: " << servers[j][0] << endl;
                    kill(pid[j], SIGKILL);
                }
                if (!isAlive && servers[j][3] == "1") // サーバーがオフラインで設定がオンラインなら起動する
                {
                    cout << "Restarting server: " << servers[j][0] << endl;
                    pid[j] = fork();
                    if (pid[j] == 0)
                    { // 子プロセス
                        cout << "child:" << j << endl;
                        if (servers[j][3] == "1")
                        {
                            // サーバーを起動する
                            filesystem::current_path(servers[j][1]);
                            string cmd = servers[j][2].substr(0, servers[j][2].find(" "));
                            string arg = servers[j][2].substr(servers[j][2].find(" ") + 1);
                            execlp(cmd.c_str(), cmd.c_str(), arg.c_str(), (char *)nullptr);
                        }
                        exit(0);
                        _exit(127);
                    }
                }
            }
            sleep(5);
        }
    }
    else if (pid[i] == 0)
    { // 子プロセス
        printf("child:%d\n", i);
        if (servers[i][3] == "1") // サーバーを起動する
        {
            cout << "Starting server: " << servers[i][0] << endl;
            filesystem::current_path(servers[i][1]);
            string cmd = servers[i][2].substr(0, servers[i][2].find(" "));
            string arg = servers[i][2].substr(servers[i][2].find(" ") + 1);
            execlp(cmd.c_str(), cmd.c_str(), arg.c_str(), (char *)nullptr);
        }
        exit(0);
        _exit(127);
    }
    else
    {
        perror("child process");
        exit(0);
    }
    return 0;
}

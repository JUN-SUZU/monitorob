#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ncurses.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string>
#include <vector>
using namespace std;

struct Server
{
    int serverId;
    int pid;
    string name;
    string path;
    string command;
    bool online;
    int cpuUsage;
    int memoryUsage;
};

void sigchld_handler(int signo)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

bool getAlive(pid_t pid)
{
    return kill(pid, 0) == 0;
}

int main()
{
    // サーバーの設定ファイルを読み込む
    vector<vector<string>> servers;
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
            P_MAX = servers.size();
            for (int j = 0; j < P_MAX; j++)
            {
                bool isAlive = getAlive(pid[j]);
                if (isAlive && servers[j][3] == "0") // サーバーがオンラインで設定がオフラインなら停止する
                {
                    cout << "Stopping server: " << servers[j][0] << endl;
                    // 孫プロセスまで全てキルする
                    kill(-pid[j], SIGKILL);
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
                            setpgid(0, 0);
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
            setpgid(0, 0);
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

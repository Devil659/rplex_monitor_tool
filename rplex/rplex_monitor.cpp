#include <ncurses.h>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <curl/curl.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <dirent.h>
#include <cmath>
#include <algorithm>
#include <unistd.h>  
#include <cstring>   
#include <ctime>   

using namespace std;

// Color pairs
#define COLOR_DEFAULT 1
#define COLOR_TITLE 2
#define COLOR_CPU 3
#define COLOR_MEM 4
#define COLOR_PROCESS 5
#define COLOR_NETWORK 6
#define COLOR_BAR 7
#define COLOR_GRAPH 8

struct ProcessInfo {
    int pid;
    string name;
    string memory;
    string cpu;
};

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

string get_ip() {
    CURL *curl;
    CURLcode res;
    string readBuffer;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.ipify.org");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if(res == CURLE_OK) {
            return readBuffer;
        }
    }
    return "Unavailable";
}

void draw_graph(WINDOW *win, int y, int x, const vector<float> &values, int width, int height, int color_pair) {
    if (values.empty()) return;

    float max_val = *max_element(values.begin(), values.end());
    if (max_val == 0) max_val = 1;

    wattron(win, COLOR_PAIR(color_pair));
    for (int i = 0; i < width && i < (int)values.size(); i++) {
        int bar_height = min(height, (int)((values[i] / max_val) * height));
        for (int j = 0; j < bar_height; j++) {
            mvwaddch(win, y + height - j - 1, x + i, ACS_CKBOARD);
        }
    }
    wattroff(win, COLOR_PAIR(color_pair));
}

string get_cpu_info() {
    ifstream cpuinfo("/proc/cpuinfo");
    string line;
    while (getline(cpuinfo, line)) {
        if (line.find("model name") != string::npos) {
            size_t pos = line.find(":");
            if (pos != string::npos) {
                string info = line.substr(pos + 2);
                if (info.length() > 40) {
                    return info.substr(0, 37) + "...";
                }
                return info;
            }
        }
    }
    return "Unknown CPU";
}

vector<float> cpu_history(60, 0); // Store 60 samples for graph

float get_cpu_usage() {
    static unsigned long long last_total = 0, last_idle = 0;
    ifstream stat("/proc/stat");
    string line;
    getline(stat, line);
    
    istringstream iss(line.substr(5));
    unsigned long long user, nice, system, idle, iowait, irq, softirq;
    iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq;
    
    unsigned long long total = user + nice + system + idle + iowait + irq + softirq;
    unsigned long long total_diff = total - last_total;
    unsigned long long idle_diff = idle - last_idle;
    
    last_total = total;
    last_idle = idle;
    
    float usage = 0.0f;
    if (total_diff > 0) {
        usage = 100.0f * (total_diff - idle_diff) / total_diff;
    }
    
    // Update history
    cpu_history.erase(cpu_history.begin());
    cpu_history.push_back(usage);
    
    return usage;
}

vector<float> mem_history(60, 0); // Store 60 samples for graph

void get_ram_info(float &total, float &used, float &percent) {
    struct sysinfo memInfo;
    sysinfo(&memInfo);
    
    total = memInfo.totalram / (1024.0f * 1024 * 1024);
    used = (memInfo.totalram - memInfo.freeram) / (1024.0f * 1024 * 1024);
    percent = (used / total) * 100.0f;
    
    // Update history
    mem_history.erase(mem_history.begin());
    mem_history.push_back(percent);
}

vector<ProcessInfo> get_processes(int max_processes = 5) {
    vector<ProcessInfo> processes;
    DIR *dir;
    struct dirent *ent;
    
    if ((dir = opendir("/proc")) != NULL) {
        while ((ent = readdir(dir)) != NULL && processes.size() < (size_t)max_processes) {
            if (ent->d_type == DT_DIR && isdigit(ent->d_name[0])) {
                int pid = atoi(ent->d_name);
                string pid_str = ent->d_name;
                
                // Get process name
                ifstream cmdline("/proc/" + pid_str + "/comm");
                string name;
                getline(cmdline, name);
                
                if (!name.empty()) {
                    // Get process stats
                    ifstream stat("/proc/" + pid_str + "/stat");
                    string stat_line;
                    getline(stat, stat_line);
                    
                    istringstream stat_iss(stat_line);
                    string dummy;
                    for (int i = 0; i < 13; i++) stat_iss >> dummy;
                    unsigned long utime, stime;
                    stat_iss >> utime >> stime;
                    float cpu_usage = (utime + stime) / sysconf(_SC_CLK_TCK);
                    
                    // Get memory usage
                    ifstream status("/proc/" + pid_str + "/status");
                    string status_line;
                    long vm_rss = 0;
                    while (getline(status, status_line)) {
                        if (status_line.find("VmRSS:") == 0) {
                            istringstream iss(status_line.substr(6));
                            iss >> vm_rss;
                            break;
                        }
                    }
                    
                    processes.push_back({
                        pid,
                        name,
                        to_string(vm_rss / 1024) + " MB",
                        to_string(cpu_usage) + "%"
                    });
                }
            }
        }
        closedir(dir);
    }
    
    // Sort by CPU usage (descending)
    sort(processes.begin(), processes.end(), [](const ProcessInfo &a, const ProcessInfo &b) {
        return stof(a.cpu.substr(0, a.cpu.size()-1)) > stof(b.cpu.substr(0, b.cpu.size()-1));
    });
    
    return processes;
}

void draw_box(WINDOW *win, int y, int x, int h, int w, const string &title) {
    wattron(win, COLOR_PAIR(COLOR_TITLE));
    mvwaddch(win, y, x, ACS_ULCORNER);
    mvwhline(win, y, x+1, ACS_HLINE, w-2);
    mvwaddch(win, y, x+w-1, ACS_URCORNER);
    mvwvline(win, y+1, x, ACS_VLINE, h-2);
    mvwvline(win, y+1, x+w-1, ACS_VLINE, h-2);
    mvwaddch(win, y+h-1, x, ACS_LLCORNER);
    mvwhline(win, y+h-1, x+1, ACS_HLINE, w-2);
    mvwaddch(win, y+h-1, x+w-1, ACS_LRCORNER);
    
    if (!title.empty()) {
        mvwprintw(win, y, x + 2, " %s ", title.c_str());
    }
    wattroff(win, COLOR_PAIR(COLOR_TITLE));
}

void init_colors() {
    start_color();
    use_default_colors();
    
    init_pair(COLOR_DEFAULT, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_TITLE, COLOR_BLACK, COLOR_CYAN);
    init_pair(COLOR_CPU, COLOR_CYAN, COLOR_BLACK);
    init_pair(COLOR_MEM, COLOR_GREEN, COLOR_BLACK);
    init_pair(COLOR_PROCESS, COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_NETWORK, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(COLOR_BAR, COLOR_BLUE, COLOR_BLACK);
    init_pair(COLOR_GRAPH, COLOR_RED, COLOR_BLACK);
}

void display_header(WINDOW *win) {
    wattron(win, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
    mvwprintw(win, 0, 2, " RPLEX System Monitor v1.4 ");
    wattroff(win, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
    
    // Show current time
    time_t now = time(0);
    tm *ltm = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", ltm);
    mvwprintw(win, 0, COLS - strlen(time_str) - 2, " %s ", time_str);
}

void display_cpu_stats(WINDOW *win, int y, int x) {
    float cpu_usage = get_cpu_usage();
    
    wattron(win, COLOR_PAIR(COLOR_CPU));
    mvwprintw(win, y, x, "CPU: %s", get_cpu_info().c_str());
    mvwprintw(win, y+1, x, "Usage: %.1f%%", cpu_usage);
    
    // Draw CPU bar
    wattron(win, COLOR_PAIR(COLOR_BAR));
    int bar_width = 25;
    int filled = (int)(bar_width * cpu_usage / 100);
    mvwprintw(win, y+1, x + 15, "[");
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) {
            waddch(win, ACS_CKBOARD);
        } else {
            waddch(win, ' ');
        }
    }
    wprintw(win, "]");
    wattroff(win, COLOR_PAIR(COLOR_BAR));
    
    // Draw CPU graph
    draw_graph(win, y+3, x, cpu_history, 60, 8, COLOR_GRAPH);
    wattroff(win, COLOR_PAIR(COLOR_CPU));
}

void display_mem_stats(WINDOW *win, int y, int x) {
    float total, used, percent;
    get_ram_info(total, used, percent);
    
    wattron(win, COLOR_PAIR(COLOR_MEM));
    mvwprintw(win, y, x, "Memory: %.1f/%.1f GB (%.1f%%)", used, total, percent);
    
    // Draw memory bar
    wattron(win, COLOR_PAIR(COLOR_BAR));
    int bar_width = 25;
    int filled = (int)(bar_width * percent / 100);
    mvwprintw(win, y+1, x, "[");
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) {
            waddch(win, ACS_CKBOARD);
        } else {
            waddch(win, ' ');
        }
    }
    wprintw(win, "]");
    wattroff(win, COLOR_PAIR(COLOR_BAR));
    
    // Draw memory graph
    draw_graph(win, y+3, x, mem_history, 60, 8, COLOR_GRAPH);
    wattroff(win, COLOR_PAIR(COLOR_MEM));
}

void display_processes(WINDOW *win, int y, int x, int width, int height) {
    draw_box(win, y, x, height, width, "Processes");
    
    auto processes = get_processes(height - 3);
    
    wattron(win, COLOR_PAIR(COLOR_PROCESS));
    mvwprintw(win, y+1, x+2, "PID");
    mvwprintw(win, y+1, x+10, "CPU%%");
    mvwprintw(win, y+1, x+18, "MEM");
    mvwprintw(win, y+1, x+26, "NAME");
    
    for (size_t i = 0; i < processes.size(); i++) {
        mvwprintw(win, y+3+i, x+2, "%5d", processes[i].pid);
        mvwprintw(win, y+3+i, x+10, "%5s", processes[i].cpu.c_str());
        mvwprintw(win, y+3+i, x+18, "%6s", processes[i].memory.c_str());
        mvwprintw(win, y+3+i, x+26, "%.20s", processes[i].name.c_str());
    }
    wattroff(win, COLOR_PAIR(COLOR_PROCESS));
}

void display_network(WINDOW *win, int y, int x, int width) {
    draw_box(win, y, x, 5, width, "Network");
    
    wattron(win, COLOR_PAIR(COLOR_NETWORK));
    mvwprintw(win, y+1, x+2, "IP: %s", get_ip().c_str());
    wattroff(win, COLOR_PAIR(COLOR_NETWORK));
}

void real_time_monitor() {
    initscr();
    curs_set(0);
    noecho();
    timeout(1000);
    keypad(stdscr, TRUE);
    
    init_colors();
    
    while (true) {
        clear();
        
        // Get terminal dimensions
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);
        
        // Draw header
        display_header(stdscr);
        
        // Draw CPU stats (top left)
        draw_box(stdscr, 2, 2, 13, max_x/2 - 2, "CPU");
        display_cpu_stats(stdscr, 3, 4);
        
        // Draw memory stats (top right)
        draw_box(stdscr, 2, max_x/2 + 1, 13, max_x/2 - 3, "Memory");
        display_mem_stats(stdscr, 3, max_x/2 + 3);
        
        // Draw processes (bottom left)
        int process_height = max_y - 16;
        if (process_height > 10) {
            draw_box(stdscr, 15, 2, process_height, max_x/2 - 2, "Top Processes");
            display_processes(stdscr, 15, 2, max_x/2 - 2, process_height);
        }
        
        // Draw network info (bottom right)
        if (max_y > 16) {
            display_network(stdscr, 15, max_x/2 + 1, max_x/2 - 3);
        }
        
        refresh();
        
        // Check for quit command
        int ch = getch();
        if (ch == 'q' || ch == 'Q') {
            break;
        }
    }
    
    endwin();
}

int main() {
    // Initialize curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Run the monitor
    real_time_monitor();
    
    // Cleanup curl
    curl_global_cleanup();
    
    return 0;
}

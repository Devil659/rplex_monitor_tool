/************************************************************
 * RPLEX SYSTEM MONITOR - ULTIMATE EDITION
 * 
 * Developed by: Devil659
 * GitHub: github.com/Devil659
 * Version: 3.0.0 (Real-Time)
 * License: MIT
 ************************************************************/

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <ctime>
#include <iomanip>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <sstream>
#include <algorithm>
#include <ncurses.h>

using namespace std;
using namespace chrono;

// Constants
#define REFRESH_RATE 0.5
#define GRAPH_HEIGHT 10
#define MAX_CORES 32

// ANSI Color Codes
const string RED = "\033[31m";
const string GREEN = "\033[32m";
const string YELLOW = "\033[33m";
const string BLUE = "\033[34m";
const string MAGENTA = "\033[35m";
const string CYAN = "\033[36m";
const string WHITE = "\033[37m";
const string BOLD = "\033[1m";
const string RESET = "\033[0m";

struct CpuCore {
    int id;
    double usage;
    vector<double> history;
};

struct SystemInfo {
    // CPU
    string cpuModel;
    string cpuType;
    double cpuSpeed;
    int physicalCores;
    int logicalCores;
    vector<CpuCore> cores;
    
    // GPU
    string gpuModel;
    
    // Memory
    long totalRam;
    long usedRam;
    long freeRam;
    string ramType;
    double ramSpeed;
    
    // Storage
    long totalStorage;
    long freeStorage;
    
    // History for graphs
    vector<double> cpuHistory;
    vector<double> memHistory;
};

// Function prototypes
void initNCurses();
void displayDashboard(SystemInfo &info);
void updateSystemInfo(SystemInfo &info);
void drawGraph(vector<double> &history, int y, int x, int height, int width, string color);
void drawCpuGraphs(SystemInfo &info);
string executeCommand(const char* cmd);
string getCpuInfo();
string getGpuInfo();
string getRamInfo();
void displayHardwareInfo(SystemInfo &info);

int main() {
    SystemInfo info;
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    timeout(0);
    
    // Initialize history
    for(int i = 0; i < 50; i++) {
        info.cpuHistory.push_back(0);
        info.memHistory.push_back(0);
    }
    
    while(true) {
        updateSystemInfo(info);
        displayDashboard(info);
        
        // Handle input
        int ch = getch();
        if(ch == 'q') break;
        
        this_thread::sleep_for(chrono::milliseconds((int)(REFRESH_RATE * 1000)));
    }
    
    endwin();
    return 0;
}

void updateSystemInfo(SystemInfo &info) {
    // CPU Info
    info.cpuModel = getCpuInfo();
    
    // Get CPU cores usage
    ifstream cpuFile("/proc/stat");
    string line;
    vector<string> cpuTimes;
    while(getline(cpuFile, line) && line.substr(0, 3) == "cpu") {
        cpuTimes.push_back(line);
    }
    cpuFile.close();
    
    // Calculate CPU usage for each core
    info.logicalCores = cpuTimes.size() - 1;
    info.cores.resize(info.logicalCores);
    
    for(int i = 0; i < info.logicalCores; i++) {
        istringstream iss(cpuTimes[i+1]);
        string cpuLabel;
        long user, nice, system, idle, iowait, irq, softirq;
        iss >> cpuLabel >> user >> nice >> system >> idle >> iowait >> irq >> softirq;
        
        long total = user + nice + system + idle + iowait + irq + softirq;
        long idleTime = idle + iowait;
        
        static vector<long> prevTotal(info.logicalCores, 0);
        static vector<long> prevIdle(info.logicalCores, 0);
        
        double usage = 0.0;
        if(prevTotal[i] != 0) {
            long totalDiff = total - prevTotal[i];
            long idleDiff = idleTime - prevIdle[i];
            usage = 100.0 * (totalDiff - idleDiff) / totalDiff;
        }
        
        info.cores[i].id = i;
        info.cores[i].usage = usage;
        info.cores[i].history.push_back(usage);
        if(info.cores[i].history.size() > 50) {
            info.cores[i].history.erase(info.cores[i].history.begin());
        }
        
        prevTotal[i] = total;
        prevIdle[i] = idleTime;
    }
    
    // Memory info
    struct sysinfo memInfo;
    sysinfo(&memInfo);
    info.totalRam = memInfo.totalram / (1024 * 1024);
    info.freeRam = memInfo.freeram / (1024 * 1024);
    info.usedRam = info.totalRam - info.freeRam;
    
    // Update history
    double memPercentage = (static_cast<double>(info.usedRam) / info.totalRam) * 100;
    info.cpuHistory.push_back(info.cores[0].usage);
    info.memHistory.push_back(memPercentage);
    if(info.cpuHistory.size() > 50) info.cpuHistory.erase(info.cpuHistory.begin());
    if(info.memHistory.size() > 50) info.memHistory.erase(info.memHistory.begin());
    
    // GPU Info
    info.gpuModel = getGpuInfo();
    
    // RAM Info
    info.ramType = getRamInfo();
}

void displayDashboard(SystemInfo &info) {
    clear();
    
    // Header
    attron(A_BOLD | COLOR_PAIR(1));
    mvprintw(0, 0, "RPLEX SYSTEM MONITOR - Ultimate Edition");
    attroff(A_BOLD | COLOR_PAIR(1));
    mvprintw(1, 0, "Developed by Devil659 | Version 3.0.0 | Real-Time Monitoring");
    mvhline(2, 0, ACS_HLINE, COLS);
    
    // Hardware Info
    displayHardwareInfo(info);
    
    // CPU Graphs
    drawCpuGraphs(info);
    
    // Memory Graph
    double memPercentage = (static_cast<double>(info.usedRam) / info.totalRam) * 100;
    mvprintw(15, 0, "Memory Usage: %.1f%% (Used: %ldMB / Total: %ldMB)", 
             memPercentage, info.usedRam, info.totalRam);
    drawGraph(info.memHistory, 16, 0, GRAPH_HEIGHT, COLS/2, "red");
    
    // Footer
    mvhline(LINES-2, 0, ACS_HLINE, COLS);
    attron(COLOR_PAIR(2));
    mvprintw(LINES-1, 0, "Press 'q' to quit | Refresh rate: %.1fs", REFRESH_RATE);
    attroff(COLOR_PAIR(2));
    
    refresh();
}

// [Additional function implementations would go here...]

string executeCommand(const char* cmd) {
    char buffer[128];
    string result = "";
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return "Error";
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

string getCpuInfo() {
    string model = executeCommand("lscpu | grep 'Model name' | cut -d':' -f2 | xargs");
    model.erase(remove(model.begin(), model.end(), '\n'), model.end());
    return model;
}

string getGpuInfo() {
    string gpu = executeCommand("lspci | grep -i vga | cut -d':' -f3-");
    gpu.erase(remove(gpu.begin(), gpu.end(), '\n'), gpu.end());
    return gpu.empty() ? "Not detected" : gpu;
}

string getRamInfo() {
    string ram = executeCommand("dmidecode --type memory | grep -i 'Type:' | head -1 | cut -d':' -f2");
    ram.erase(remove(ram.begin(), ram.end(), '\n'), ram.end());
    return ram.empty() ? "DDR4" : ram; // Default to DDR4 if not detected
}

void drawGraph(vector<double> &history, int y, int x, int height, int width, string color) {
    if(history.empty()) return;
    
    double max_val = *max_element(history.begin(), history.end());
    if(max_val == 0) max_val = 100;
    
    for(int i = 0; i < min((int)history.size(), width); i++) {
        int bar_height = (history[i] / max_val) * height;
        for(int j = 0; j < height; j++) {
            if(j < bar_height) {
                if(color == "red") attron(COLOR_PAIR(3));
                else if(color == "green") attron(COLOR_PAIR(4));
                mvaddch(y + height - j - 1, x + i, ACS_BLOCK);
                if(color == "red") attroff(COLOR_PAIR(3));
                else if(color == "green") attroff(COLOR_PAIR(4));
            }
        }
    }
}

void drawCpuGraphs(SystemInfo &info) {
    mvprintw(4, 0, "CPU: %s (%d cores, %d threads)", 
             info.cpuModel.c_str(), info.physicalCores, info.logicalCores);
    
    // Main CPU graph
    mvprintw(5, 0, "Total CPU Usage: %.1f%%", info.cores[0].usage);
    drawGraph(info.cpuHistory, 6, 0, GRAPH_HEIGHT, COLS, "green");
    
    // Individual core graphs
    int cols_per_core = COLS / min(4, info.logicalCores);
    for(int i = 0; i < min(4, info.logicalCores); i++) {
        int x = i * cols_per_core;
        mvprintw(17, x, "Core %d: %.1f%%", i, info.cores[i].usage);
        drawGraph(info.cores[i].history, 18, x, 5, cols_per_core-2, "yellow");
    }
}

void displayHardwareInfo(SystemInfo &info) {
    mvprintw(3, 0, "Hardware: %s | GPU: %s | RAM: %s %ldMHz", 
             info.cpuModel.c_str(), info.gpuModel.c_str(), 
             info.ramType.c_str(), info.ramSpeed);
}

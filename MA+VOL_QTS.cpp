#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <iomanip>
using namespace std;

struct TradeRecord {
    string date;
    string action;
    long double price;
    long double shares;
    long double commission;
    long double cash_after;
};

struct DailyMARecord {
    string date;
    long double price;
    long double shortMA;
    long double longMA;
    long double volume;
    bool isGoldenCross;
    bool isDeathCross;
};

const int    NUM_PARTICLES = 30;
const int    NUM_GENS = 1000;
const int    NUM_BITS = 16;
const double THETA = 0.002;
const double COMMISSION = 0.0008;
const string CSV_PATH = "C:/Users/Lab114/source/repos/NEW_S_T_O_C_K/DJIA30.csv";
const string TICKER = "AAPL";
const string BACKTEST_START = "2024/01/01";
const string BACKTEST_END = "2024/12/31";

string normalizeDate(const string& date) {
    size_t pos1 = date.find('/');
    char separator = '/';

    if (pos1 == string::npos) {
        pos1 = date.find('-');
        separator = '-';
    }

    if (pos1 == string::npos) return date;

    size_t pos2 = date.find(separator, pos1 + 1);
    if (pos2 == string::npos) return date;

    string year = date.substr(0, pos1);
    string month = date.substr(pos1 + 1, pos2 - pos1 - 1);
    string day = date.substr(pos2 + 1);

    if (month.length() == 1) month = "0" + month;
    if (day.length() == 1) day = "0" + day;

    return year + "/" + month + "/" + day;
}

void showDateRange(const string& csvPath) {
    ifstream fin(csvPath);
    if (!fin.is_open()) return;

    string line;
    getline(fin, line);

    vector<string> dates;
    int count = 0;
    while (getline(fin, line) && count < 5) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        istringstream ss(line);
        string date;
        getline(ss, date, ',');
        dates.push_back(date);
        count++;
    }

    fin.seekg(0);
    getline(fin, line);

    vector<string> lastDates;
    string temp;
    while (getline(fin, temp)) {
        if (!temp.empty() && temp.back() == '\r') temp.pop_back();
        lastDates.push_back(temp);
    }

    cout << "\n=== CSV 日期範圍 ===\n";
    cout << "首次日期:\n";
    for (int i = 0; i < (int)dates.size(); i++) {
        cout << "  " << dates[i] << "\n";
    }

    if (lastDates.size() > 0) {
        cout << "最後日期:\n";
        int start = max(0, (int)lastDates.size() - 5);
        for (int i = start; i < (int)lastDates.size(); i++) {
            if (!lastDates[i].empty()) {
                istringstream ss(lastDates[i]);
                string date;
                getline(ss, date, ',');
                cout << "  " << date << "\n";
            }
        }
    }
    cout << "\n";
}

vector<long double> loadCloses(const string& csvPath, const string& ticker,
    const string& startDate, const string& endDate,
    int& outStartIdx, int& outEndIdx,
    vector<string>& outDates, vector<long double>& outVolumes, int warmupDays = -1) {
    ifstream fin(csvPath);
    if (!fin.is_open()) { cerr << "無法打開檔案: " << csvPath << endl; exit(1); }

    string line;
    getline(fin, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();

    vector<string> headers;
    {
        istringstream ss(line);
        string tok;
        while (getline(ss, tok, ',')) {
            if (!tok.empty() && tok.back() == '\r') tok.pop_back();

            int start = 0, end = (int)tok.length() - 1;
            while (start <= end && isspace(tok[start])) start++;
            while (end >= start && isspace(tok[end])) end--;

            if (start <= end) {
                tok = tok.substr(start, end - start + 1);
            }
            else {
                tok = "";
            }

            headers.push_back(tok);
        }
    }

    string target = ticker + "_Close";
    string volumeTarget = ticker + "_Volume";
    int closeCol = -1, dateCol = -1, volumeCol = -1;
    for (int i = 0; i < (int)headers.size(); i++) {
        if (headers[i] == target) { closeCol = i; }
        if (headers[i] == "Date") { dateCol = i; }
        if (headers[i] == volumeTarget) { volumeCol = i; }
    }
    if (closeCol < 0) {
        cerr << "未找到欄: " << target << endl;
        cerr << "可用的欄:\n";
        for (int i = 0; i < (int)headers.size(); i++) {
            cerr << "  [" << i << "] \"" << headers[i] << "\"\n";
        }
        exit(1);
    }

    vector<long double> closes;
    vector<string> dates;
    vector<long double> volumes;
    vector<long double> preDates_closes;
    vector<long double> preDates_volumes;
    vector<string> preDates_strs;
    outStartIdx = -1;
    outEndIdx = -1;
    bool foundStart = false;

    while (getline(fin, line)) {
        if (line.empty() || line == "\r") continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        istringstream ss(line);
        string tok;
        int col = 0;
        string date = "";
        long double close_price = 0.0L;
        long double volume = 0.0L;
        bool hasClose = false;

        while (getline(ss, tok, ',')) {
            int start = 0, end = (int)tok.length() - 1;
            while (start <= end && isspace(tok[start])) start++;
            while (end >= start && isspace(tok[end])) end--;

            if (start <= end) {
                tok = tok.substr(start, end - start + 1);
            }
            else {
                tok = "";
            }

            if (col == dateCol) {
                date = normalizeDate(tok);
            }

            if (col == closeCol) {
                try {
                    close_price = stold(tok);
                    hasClose = true;
                }
                catch (...) { hasClose = false; }
            }

            if (col == volumeCol) {
                try {
                    volume = stold(tok);
                }
                catch (...) { volume = 0.0L; }
            }
            col++;
        }

        if (!hasClose || date.empty()) continue;

        if (!foundStart && date < startDate) {
            preDates_closes.push_back(close_price);
            preDates_volumes.push_back(volume);
            preDates_strs.push_back(date);
        }
        else if (date >= startDate && date <= endDate) {
            if (!foundStart) {
                foundStart = true;
                int warmup_to_use = warmupDays;
                if (warmup_to_use < 0) {
                    warmup_to_use = (int)preDates_closes.size();
                }
                else {
                    warmup_to_use = min(warmup_to_use, (int)preDates_closes.size());
                }

                int skipFrom = max(0, (int)preDates_closes.size() - warmup_to_use);
                for (int i = skipFrom; i < (int)preDates_closes.size(); i++) {
                    closes.push_back(preDates_closes[i]);
                    volumes.push_back(preDates_volumes[i]);
                    dates.push_back(preDates_strs[i]);
                }
                outStartIdx = (int)closes.size();
            }
            closes.push_back(close_price);
            volumes.push_back(volume);
            dates.push_back(date);
            outEndIdx = (int)closes.size() - 1;
        }
        else if (date > endDate) {
            break;
        }
    }

    if (outStartIdx < 0) {
        cerr << "錯誤: 未找到開始日期 " << startDate << endl;
        exit(1);
    }
    if (outEndIdx < 0) {
        cerr << "錯誤: 未找到結束日期 " << endDate << endl;
        exit(1);
    }

    outDates = dates;

    cout << "\n*** CSV 數據加載 ***\n";
    if (warmupDays >= 0) {
        cout << "QTS 最佳化: 預熱天數限制 = " << warmupDays << "\n";
    }
    else {
        cout << "驗證階段: 加載所有可用數據\n";
    }
    cout << "預熱期（在開始之前): " << outStartIdx << "\n";
    if (outStartIdx > 0) {
        cout << "  正常 - 預熱數據可用\n";
        cout << "  最早日期: " << dates[0] << "\n";
        cout << "  最新日期: " << dates[outStartIdx - 1] << "\n";
        cout << "  最後 5 個預熱數據:\n";
        int startShow = max(0, outStartIdx - 5);
        for (int i = startShow; i < outStartIdx; i++) {
            cout << "    [" << i << "] " << dates[i] << ": 價格=" << fixed << setprecision(2) << closes[i]
                << " 交易量=" << fixed << setprecision(0) << volumes[i] << "\n";
        }
    }
    else {
        cout << "  警告: 預熱期為 0!\n";
        cout << "     移動平均線計算可能不正確!\n";
    }
    cout << "回測期間（開始至結束): " << (outEndIdx - outStartIdx + 1) << "\n";
    cout << "  開始日期: " << dates[outStartIdx] << "\n";
    cout << "  結束日期: " << dates[outEndIdx] << "\n";
    cout << "  首次 3 個回測數據:\n";
    for (int i = outStartIdx; i <= min(outStartIdx + 2, outEndIdx); i++) {
        cout << "    [" << i << "] " << dates[i] << ": Price=" << fixed << setprecision(2) << closes[i]
            << " Volume=" << fixed << setprecision(0) << volumes[i] << "\n";
    }
    cout << "總計: " << dates.size() << "\n";
    cout << "\n*** 日期格式驗證 ***\n";
    cout << "  輸入開始日期: \"" << startDate << "\"\n";
    cout << "  輸入結束日期: \"" << endDate << "\"\n";
    cout << "  CSV 實際開始: \"" << dates[outStartIdx] << "\"\n";
    cout << "  CSV 實際結束: \"" << dates[outEndIdx] << "\"\n";
    cout << "\n";

    outDates = dates;
    outVolumes = volumes;
    return closes;
}

vector<long double> computeMA(const vector<long double>& closes, int window) {
    vector<long double> ma;
    if ((int)closes.size() < window || window <= 0) return ma;

    long double sum = 0.0;
    for (int i = 0; i < window; i++)
        sum += closes[i];

    for (int i = 0; i < window - 1; i++) {
        ma.push_back(0.0);
    }

    ma.push_back(sum / window);

    for (int i = window; i < (int)closes.size(); i++) {
        sum += closes[i];
        sum -= closes[i - window];
        ma.push_back(sum / window);
    }
    return ma;
}

long double calcCommission(long double price, long double shares) {
    return price * shares * COMMISSION;
}

int bitsToInt(const int* bits, int start) {
    int val = 0;
    for (int i = 0; i < 8; i++) val = val * 2 + bits[start + i];
    return val + 1;
}

double Q[16];
int    particles[30][16];
int    bestsol[16];
int    westsol[16];
long double best_fitness;
long double west_fitness;
int    g_outputStartIdx = 0;
int    g_outputEndIdx = -1;
vector<TradeRecord> g_bestTrades;
vector<string> g_dates;
vector<DailyMARecord> g_bestDailyMA;
vector<long double> g_closes;
vector<long double> g_volumes;
bool g_debugMode = false;

long double evaluate(int B1, int B2, const vector<long double>& closes, const vector<long double>& volumes,
    vector<TradeRecord>* trades = nullptr, vector<DailyMARecord>* dailyMA = nullptr) {
    const long double epsilon = 1e-6;

    if (trades) trades->clear();
    if (dailyMA) dailyMA->clear();

    if (g_debugMode) {
        g_closes = closes;
    }

    vector<long double> maS = computeMA(closes, B1);
    vector<long double> maL = computeMA(closes, B2);

    int shortLen = (int)maS.size();
    int longLen = (int)maL.size();

    if (shortLen < 2 || longLen < 2) return -1e18;

    int startIdx = g_outputStartIdx;
    int endIdx = g_outputEndIdx;

    if (startIdx > endIdx) return -1e18;

    long double cash = 10000.0L, shares = 0.0L;
    bool holding = false;

    for (int closesIdx = startIdx; closesIdx <= endIdx; closesIdx++) {
        int maIdxS = closesIdx;
        int maIdxL = closesIdx;

        if (maIdxS < 0 || maIdxL < 0) continue;
        if (maIdxS >= (int)maS.size() || maIdxL >= (int)maL.size()) continue;

        bool sValid = (maS[maIdxS] > 0);
        bool lValid = (maL[maIdxL] > 0);

        long double sTody = maS[maIdxS];
        long double lTody = maL[maIdxL];
        long double price = closes[closesIdx];

        if (!sValid || !lValid) {
            if (dailyMA) {
                DailyMARecord daily;
                daily.date = (closesIdx >= 0 && closesIdx < (int)g_dates.size()) ? g_dates[closesIdx] : "";
                daily.price = price;
                daily.shortMA = sTody;
                daily.longMA = lTody;
                daily.volume = (closesIdx >= 0 && closesIdx < (int)volumes.size()) ? volumes[closesIdx] : 0;
                daily.isGoldenCross = false;
                daily.isDeathCross = false;
                dailyMA->push_back(daily);
            }
            continue;
        }

        long double sYest = (maIdxS > 0) ? maS[maIdxS - 1] : 0;
        long double lYest = (maIdxL > 0) ? maL[maIdxL - 1] : 0;

        bool isGoldenCross = (sYest - lYest < epsilon) && (sTody - lTody > epsilon);
        bool isDeathCross = (sYest - lYest > -epsilon) && (sTody - lTody < -epsilon);

        if (dailyMA) {
            DailyMARecord daily;
            daily.date = (closesIdx >= 0 && closesIdx < (int)g_dates.size()) ? g_dates[closesIdx] : "";
            daily.price = price;
            daily.shortMA = sTody;
            daily.longMA = lTody;
            daily.volume = (closesIdx >= 0 && closesIdx < (int)volumes.size()) ? volumes[closesIdx] : 0;
            daily.isGoldenCross = isGoldenCross;
            daily.isDeathCross = isDeathCross;
            dailyMA->push_back(daily);
        }

        if (!holding && isGoldenCross) {
            long double effectivePrice = price * (1.0 + COMMISSION);
            shares = (long double)((long long)(cash / effectivePrice));

            if (shares > 0) {
                long double commission = calcCommission(price, shares);
                long double totalCost = shares * price + commission;
                cash -= totalCost;
                holding = true;

                if (trades) {
                    TradeRecord rec;
                    rec.date = (closesIdx >= 0 && closesIdx < (int)g_dates.size()) ? g_dates[closesIdx] : "";
                    rec.action = "買入";
                    rec.price = price;
                    rec.shares = shares;
                    rec.commission = commission;
                    rec.cash_after = cash;
                    trades->push_back(rec);
                }
            }
        }
        else if (holding && isDeathCross) {
            long double revenue = shares * price;
            long double commission = calcCommission(price, shares);
            cash += revenue - commission;

            if (trades) {
                TradeRecord rec;
                rec.date = (closesIdx >= 0 && closesIdx < (int)g_dates.size()) ? g_dates[closesIdx] : "";
                rec.action = "賣出";
                rec.price = price;
                rec.shares = shares;
                rec.commission = commission;
                rec.cash_after = cash;
                trades->push_back(rec);
            }

            shares = 0.0L;
            holding = false;
        }
    }

    if (holding) {
        long double price = closes.back();
        long double revenue = shares * price;
        long double commission = calcCommission(price, shares);
        cash += revenue - commission;

        if (trades) {
            TradeRecord rec;
            rec.date = (int)closes.size() - 1 >= 0 && (int)closes.size() - 1 < (int)g_dates.size() ?
                g_dates[(int)closes.size() - 1] : "";
            rec.action = "平倉";
            rec.price = price;
            rec.shares = shares;
            rec.commission = commission;
            rec.cash_after = cash;
            trades->push_back(rec);
        }
    }
    return cash - 10000.0L;
}

void initialization() {
    for (int i = 0; i < 16; i++) Q[i] = 0.5;
}

void measure() {
    for (int k = 0; k < 30; k++)
        for (int i = 0; i < 16; i++)
            particles[k][i] = (Q[i] > rand() / (double)RAND_MAX) ? 1 : 0;
}

void calculation(const vector<long double>& closes) {
    best_fitness = -1e18;
    west_fitness = 1e18;
    for (int k = 0; k < 30; k++) {
        int B1 = bitsToInt(particles[k], 0);
        int B2 = bitsToInt(particles[k], 8);

        vector<TradeRecord> trades;
        vector<DailyMARecord> dailyMA;
        long double fit = evaluate(B1, B2, closes, g_volumes, &trades, &dailyMA);

        if (fit > best_fitness) {
            best_fitness = fit;
            g_bestTrades = trades;
            g_bestDailyMA = dailyMA;
            for (int i = 0; i < 16; i++) bestsol[i] = particles[k][i];
        }
        if (fit < west_fitness) {
            west_fitness = fit;
            for (int i = 0; i < 16; i++) westsol[i] = particles[k][i];
        }
    }
}

void update() {
    for (int i = 0; i < 16; i++) {
        if (bestsol[i] == 1 && westsol[i] == 0) Q[i] += 0.002;
        else if (bestsol[i] == 0 && westsol[i] == 1) Q[i] -= 0.002;
    }
}

int main() {
    srand(114);

    cout << "正在加載股票數據: " << TICKER << "\n";
    cout << "回測期間: " << BACKTEST_START << " 至 " << BACKTEST_END << "\n";

    showDateRange(CSV_PATH);

    int warmupDays = 255;
    vector<long double> closes = loadCloses(CSV_PATH, TICKER, BACKTEST_START, BACKTEST_END,
        g_outputStartIdx, g_outputEndIdx, g_dates, g_volumes, warmupDays);
    cout << "總共加載: " << closes.size() << "\n";
    cout << "  - 預熱期: " << g_outputStartIdx << " (用於移動平均線計算)\n";
    cout << "  - 回測期: " << (g_outputEndIdx - g_outputStartIdx + 1) << "\n\n";

    int         gbest_B1 = 0, gbest_B2 = 0, best_gen = -1;
    long double gbest_fitness = -1e18;

    cout << "代\tB1\tB2\t適應度\t\t\t最佳\n";
    cout << string(75, '-') << "\n";

    initialization();
    cout << fixed << setprecision(0);

    for (int gen = 0; gen < 1000; gen++) {
        measure();

        if (gen == 0) {
            g_debugMode = true;
        }

        calculation(closes);
        g_debugMode = false;

        update();

        int curB1 = bitsToInt(bestsol, 0);
        int curB2 = bitsToInt(bestsol, 8);

        if (best_fitness > gbest_fitness) {
            gbest_fitness = best_fitness;
            gbest_B1 = curB1;
            gbest_B2 = curB2;
            best_gen = gen + 1;
        }

        cout << (gen + 1) << "\t" << curB1 << "\t" << curB2
            << "\t" << (double)best_fitness
            << "\t\t" << (double)gbest_fitness << "\n";
    }

    cout << "=== 最終結果 ===\n";
    cout << "最佳 B1 (短期移動平均線): " << gbest_B1 << "\n";
    cout << "最佳 B2 (長期移動平均線): " << gbest_B2 << "\n";

    if (gbest_B1 > gbest_B2) {
        cout << "\n警告: 短期移動平均線 (" << gbest_B1 << ") > 長期移動平均線 (" << gbest_B2 << ")\n";
        cout << "   交叉邏輯可能已反轉!\n\n";
    }

    cout << "最佳利潤: " << (double)gbest_fitness << "\n";
    cout << "代數: " << best_gen << "\n";

    int warmupB1 = gbest_B1 - 1;
    int warmupB2 = gbest_B2 - 1;
    int warmupNeeded = max(gbest_B1, gbest_B2) - 1;

    cout << "\n*** 所需預熱天數 ***\n";
    cout << "開始日期: " << BACKTEST_START << "\n";
    cout << "B1=" << gbest_B1 << " 需要 " << warmupB1 << " 個交易日在開始之前\n";
    cout << "B2=" << gbest_B2 << " 需要 " << warmupB2 << " 個交易日在開始之前\n";
    cout << "實際所需: max(" << gbest_B1 << "," << gbest_B2 << ") - 1 = " << warmupNeeded << "\n";

    if (g_outputStartIdx >= warmupNeeded) {
    }
    else {
        cout << "\n警告: 預熱期不足!\n";
    }

    g_bestTrades.clear();
    g_bestDailyMA.clear();

    long double final_fitness = evaluate(gbest_B1, gbest_B2, closes, g_volumes, &g_bestTrades, &g_bestDailyMA);

    cout << "\n=== 交易詳情 ===\n";
    cout << "序號.\t日期\t\t操作\t\t價格\t\t股份\t\t佣金\t\t現金\n";
    cout << string(110, '-') << "\n";

    long double totalCommission = 0.0;
    for (int i = 0; i < (int)g_bestTrades.size(); i++) {
        const TradeRecord& rec = g_bestTrades[i];
        totalCommission += rec.commission;

        cout << (i + 1) << "\t" << rec.date << "\t" << rec.action << "\t"
            << fixed << setprecision(2) << rec.price << "\t"
            << rec.shares << "\t"
            << rec.commission << "\t"
            << rec.cash_after << "\n";
    }

    cout << string(110, '-') << "\n";
    cout << "交易筆數: " << g_bestTrades.size() << "\n";
    cout << "總佣金: " << fixed << setprecision(2) << totalCommission << "\n";
    cout << "初始資本: 10000.00\n";
    cout << "最終資產: " << (double)gbest_fitness + 10000.0 << "\n";

    cout << "\n=== 每日移動平均線數據 ===\n";
    cout << "日期\t\t價格\t\t短期MA\t\t長期MA\t\t交易量\t\t信號\n";
    cout << string(115, '-') << "\n";

    for (int i = 0; i < (int)g_bestDailyMA.size(); i++) {
        const DailyMARecord& daily = g_bestDailyMA[i];
        string signal = "";
        if (daily.isGoldenCross) signal = "黃金交叉";
        else if (daily.isDeathCross) signal = "死亡交叉";

        cout << daily.date << "\t"
            << fixed << setprecision(2) << daily.price << "\t\t"
            << fixed << setprecision(6) << daily.shortMA << "\t\t"
            << fixed << setprecision(6) << daily.longMA << "\t\t"
            << fixed << setprecision(0) << daily.volume << "\t\t"
            << signal << "\n";
    }

    cout << string(115, '-') << "\n";
    cout << "總天數: " << g_bestDailyMA.size() << "\n";

    return 0;
}

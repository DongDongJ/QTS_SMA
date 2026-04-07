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

// ============================================================
//  交易記錄結構
// ============================================================
struct TradeRecord {
    string date;            // 交易日期
    string action;          // 買入/賣出/期末平倉
    long double price;
    long double shares;
    long double commission;
    long double cash_after;
};

// ============================================================
//  每日 MA 記錄結構
// ============================================================
struct DailyMARecord {
    string date;
    long double price;
    long double shortMA;
    long double longMA;
    long double volume;
    long double shortVolumeMA;
    long double longVolumeMA;
    bool isGoldenCross;
    bool isDeathCross;
};

// ============================================================
//  參數設定
// ============================================================
const int    NUM_PARTICLES = 30;
const int    NUM_GENS = 1000;
const int    NUM_BITS = 32;   // bit0~7: B1, bit8~15: B2, bit16~23: V1, bit24~31: V2
const double THETA = 0.002;
const double COMMISSION = 0.0008;  // 0.08%
const string CSV_PATH = "C:/Users/Lab114/source/repos/NEW_S_T_O_C_K/DJIA30.csv";
const string TICKER = "AAPL";
const string BACKTEST_START = "2024/01/01";    // 回測開始日期 (YYYY/MM/DD)
const string BACKTEST_END = "2024/12/31";    // 回測結束日期 (YYYY/MM/DD)

// ============================================================
//  日期規範化：將 YYYY/M/D 轉換為 YYYY/MM/DD 格式
//  例如：2024/1/1 → 2024/01/01
// ============================================================
string normalizeDate(const string& date) {
    size_t pos1 = date.find('/');
    if (pos1 == string::npos) return date;

    size_t pos2 = date.find('/', pos1 + 1);
    if (pos2 == string::npos) return date;

    string year = date.substr(0, pos1);
    string month = date.substr(pos1 + 1, pos2 - pos1 - 1);
    string day = date.substr(pos2 + 1);

    // 補零到兩位數
    if (month.length() == 1) month = "0" + month;
    if (day.length() == 1) day = "0" + day;

    return year + "/" + month + "/" + day;
}

// ============================================================
//  顯示 CSV 中的日期範圍（調試用）
// ============================================================
void showDateRange(const string& csvPath) {
    ifstream fin(csvPath);
    if (!fin.is_open()) return;

    string line;
    getline(fin, line);  // 跳過標題

    vector<string> dates;
    int count = 0;
    while (getline(fin, line) && count < 5) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        istringstream ss(line);
        string date;
        getline(ss, date, ',');  // 取第一欄（日期）
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

    cout << "\n=== CSV 日期範圍診斷 ===\n";
    cout << "開頭幾筆日期:\n";
    for (int i = 0; i < (int)dates.size(); i++) {
        cout << "  " << dates[i] << "\n";
    }

    if (lastDates.size() > 0) {
        cout << "結尾幾筆日期:\n";
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

// ============================================================
//  讀取 CSV，取出指定股票的收盤價和成交量（支持時間範圍）
//  策略：加載 START 前所有可用的歷史數據 + START 至 END 的回測數據
//  這樣能支持任意 B1、B2、V1、V2 組合的 MA 計算
//  warmupDays: 精確控制預熱天數（-1 表示載入全部歷史）
// ============================================================
vector<long double> loadCloses(const string& csvPath, const string& ticker,
    const string& startDate, const string& endDate,
    int& outStartIdx, int& outEndIdx,
    vector<string>& outDates, vector<long double>& outVolumes, int warmupDays = -1) {
    ifstream fin(csvPath);
    if (!fin.is_open()) { cerr << "無法開啟檔案: " << csvPath << endl; exit(1); }

    string line;
    getline(fin, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();

    vector<string> headers;
    {
        istringstream ss(line); string tok;
        while (getline(ss, tok, ',')) {
            if (!tok.empty() && tok.back() == '\r') tok.pop_back();

            // 去除前後空格
            int start = 0, end = tok.length() - 1;
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
        cerr << "找不到欄位: " << target << endl;
        cerr << "CSV 中找到的欄位有：\n";
        for (int i = 0; i < (int)headers.size(); i++) {
            cerr << "  [" << i << "] \"" << headers[i] << "\"\n";
        }
        exit(1);
    }

    vector<long double> closes;
    vector<string> dates;
    vector<long double> preDates_closes;  // 暫存 START 前的數據
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

        while (getline(ss, tok, ',')) {
            // 去除前後空格
            int start = 0, end = tok.length() - 1;
            while (start <= end && isspace(tok[start])) start++;
            while (end >= start && isspace(tok[end])) end--;

            if (start <= end) {
                tok = tok.substr(start, end - start + 1);
            }
            else {
                tok = "";
            }

            if (col == dateCol) { date = normalizeDate(tok); }
            if (col == closeCol) {
                // START 前加載所有可用數據
                if (!foundStart && date < startDate) {
                    try {
                        preDates_closes.push_back(stold(tok));
                        preDates_strs.push_back(date);
                    }
                    catch (...) {}
                }
                // START 至 END 加載回測數據
                else if (date >= startDate && date <= endDate) {
                    if (!foundStart) {
                        foundStart = true;
                        // 精確控制預熱天數
                        int warmup_to_use = warmupDays;
                        if (warmup_to_use < 0) {
                            // 載入全部歷史數據（用於最終精確驗證）
                            warmup_to_use = (int)preDates_closes.size();
                        } else {
                            // 只取最後 warmupDays 筆預熱數據
                            warmup_to_use = min(warmup_to_use, (int)preDates_closes.size());
                        }

                        // 將精確的預熱數據加入 closes
                        int skipFrom = max(0, (int)preDates_closes.size() - warmup_to_use);
                        for (int i = skipFrom; i < (int)preDates_closes.size(); i++) {
                            closes.push_back(preDates_closes[i]);
                            dates.push_back(preDates_strs[i]);
                        }
                        outStartIdx = (int)closes.size();
                    }
                    try {
                        closes.push_back(stold(tok));
                        dates.push_back(date);
                        outEndIdx = (int)closes.size() - 1;
                    }
                    catch (...) {}
                }
                // END 之後停止加載
                else if (date > endDate) {
                    break;
                }
                break;
            }
            col++;
        }
    }

    // 檢查日期範圍
    if (outStartIdx < 0) {
        cerr << "錯誤: 找不到起始日期 " << startDate << endl;
        exit(1);
    }
    if (outEndIdx < 0) {
        cerr << "錯誤: 找不到結束日期 " << endDate << endl;
        exit(1);
    }

    outDates = dates;  // 返回日期

    cout << "\n*** CSV 數據加載情況 ***\n";
    if (warmupDays >= 0) {
        cout << "QTS優化階段：預熱天數限制 = " << warmupDays << " 筆\n";
    } else {
        cout << "精確驗證階段：無限制載入全部預熱數據\n";
    }
    cout << "預熱期（START 前）: " << outStartIdx << " 筆\n";
    if (outStartIdx > 0) {
        cout << "  ✅ 預熱期有數據\n";
        cout << "  最早日期: " << dates[0] << "\n";
        cout << "  最晚日期: " << dates[outStartIdx - 1] << "\n";
        cout << "  預熱期具体数据 (最後5筆):\n";
        int startShow = max(0, outStartIdx - 5);
        for (int i = startShow; i < outStartIdx; i++) {
            cout << "    [" << i << "] " << dates[i] << ": " << fixed << setprecision(2) << closes[i] << "\n";
        }
    }
    else {
        cout << "  ❌ 【警告】預熱期為 0！沒有加載任何預熱數據！\n";
        cout << "     這會導致 MA 計算錯誤！短期和長期 MA 會全是 0 或不正確！\n";
    }
    cout << "回測期（START 至 END）: " << (outEndIdx - outStartIdx + 1) << " 筆\n";
    cout << "  開始日期: " << dates[outStartIdx] << "\n";
    cout << "  結束日期: " << dates[outEndIdx] << "\n";
    cout << "  回測期具體數據 (首3筆):\n";
    for (int i = outStartIdx; i <= min(outStartIdx + 2, outEndIdx); i++) {
        cout << "    [" << i << "] " << dates[i] << ": " << fixed << setprecision(2) << closes[i] << "\n";
    }
    cout << "總計: " << dates.size() << " 筆\n";
    cout << "\n【日期格式驗証】\n";
    cout << "  指定開始日期: \"" << startDate << "\"\n";
    cout << "  指定結束日期: \"" << endDate << "\"\n";
    cout << "  CSV 實際開始日期: \"" << dates[outStartIdx] << "\"\n";
    cout << "  CSV 實際結束日期: \"" << dates[outEndIdx] << "\"\n";
    cout << "\n";

    return closes;
}

// ============================================================
//  計算簡單移動平均 (SMA) - 往前向（只看過去數據）
//  回傳長度 = closes.size()（與 closes 長度相同）
//  ma[i] = closes[i-window+1] 到 closes[i] 的平均值
//  前 window-1 個位置填充為 0（表示數據不足）
//  第一個有效的 MA 是 ma[window-1]
// ============================================================
vector<long double> computeMA(const vector<long double>& closes, int window) {
    vector<long double> ma;
    if ((int)closes.size() < window || window <= 0) return ma;

    // 往前向 MA：ma[i] = closes[i-window+1] 到 closes[i] 的平均
    // 第一個有效的 MA 是 ma[window-1]
    long double sum = 0.0;
    for (int i = 0; i < window; i++)
        sum += closes[i];

    // 填充前面的無效位置
    for (int i = 0; i < window - 1; i++) {
        ma.push_back(0.0);  // 0 表示無效（數據不足）
    }

    // 第一個有效的 MA
    ma.push_back(sum / window);

    // 後續 MA
    for (int i = window; i < (int)closes.size(); i++) {
        sum += closes[i];
        sum -= closes[i - window];
        ma.push_back(sum / window);
    }
    return ma;
}

// ============================================================
//  計算手續費
// ============================================================
long double calcCommission(long double price, long double shares) {
    return price * shares * COMMISSION;
}

// ============================================================
//  8 個 bit 轉整數，範圍 1~256
//  bits[start] 為最高位元
// ============================================================
int bitsToInt(const int* bits, int start) {
    int val = 0;
    for (int i = 0; i < 8; i++) val = val * 2 + bits[start + i];
    return val + 1;  // 0~255 -> 1~256
}

// ============================================================
//  QTS 全域變數
// ============================================================
double Q[NUM_BITS];
int    particles[NUM_PARTICLES][NUM_BITS];
int    bestsol[NUM_BITS];
int    westsol[NUM_BITS];
long double best_fitness;
long double west_fitness;
int    g_outputStartIdx = 0;  // 回測起始索引（全域變數）
int    g_outputEndIdx = -1;   // 回測結束索引（全域變數）
vector<TradeRecord> g_bestTrades;  // 保存最佳解的交易明細
vector<string> g_dates;  // 保存所有日期
vector<DailyMARecord> g_bestDailyMA;  // 保存最佳解的每日 MA 數據
vector<long double> g_closes;  // 保存所有收盤價（用於調試）
vector<long double> g_volumes;  // 保存所有成交量
bool g_debugMode = false;  // 調試模式標誌

// ============================================================
//  適應度：MA 黃金/死亡交叉策略，回傳總報酬（扣手續費）
//  B1, B2: 價格 MA 周期
//  V1, V2: 成交量 MA 周期
// ============================================================
long double evaluate(int B1, int B2, int V1, int V2, const vector<long double>& closes, const vector<long double>& volumes, vector<TradeRecord>* trades = nullptr, vector<DailyMARecord>* dailyMA = nullptr) {
    const long double epsilon = 1e-6;  // 浮點數精度容差（提高到 1e-6 以處理顯示精度下的浮點誤差）

    if (trades) trades->clear();  // 清空交易記錄
    if (dailyMA) dailyMA->clear();  // 清空每日 MA 記錄

    // 調試：保存 closes 供後續使用
    if (g_debugMode) {
        g_closes = closes;
    }

    vector<long double> maS = computeMA(closes, B1);  // 價格短期MA
    vector<long double> maL = computeMA(closes, B2);  // 價格長期MA
    vector<long double> volmaS = computeMA(volumes, V1);  // 成交量短期MA
    vector<long double> volmaL = computeMA(volumes, V2);  // 成交量長期MA

    int shortLen = (int)maS.size();
    int longLen = (int)maL.size();

    if (shortLen < 2 || longLen < 2) return -1e18;

    // DEBUG: 顯示 MA 數組的診斷摘要
    if (g_debugMode) {
        cout << "\n=== MA 數組計算診斷 ===\n";
        cout << "Data: " << closes.size() << " 個收盤價\n";
        cout << "B1 (短期)=" << B1 << " 天: maS[0-" << (B1 - 2) << "] = 0(預熱中), maS[" << (B1 - 1) << "~] = 有效值\n";
        cout << "B2 (長期)=" << B2 << " 天: maL[0-" << (B2 - 2) << "] = 0(預熱中), maL[" << (B2 - 1) << "~] = 有效值\n";
        cout << "======================\n\n";
    }

    // maS[i] 對應 closes[i + B1 - 1]
    // maL[i] 對應 closes[i + B2 - 1]
    // 要処理 closes[closesIdx]，需要 maS 的索引是 closesIdx - B1 + 1，maL 的索引是 closesIdx - B2 + 1

    // 交易開始點在 g_outputStartIdx，直接使用（不強制要求完整預熱）
    // 對於沒有足夠歷史數據的初始日期，會自動使用可用的數據
    int startIdx = g_outputStartIdx;
    int endIdx = g_outputEndIdx;

    // DEBUG: 检查索引范围是否合理
    if (g_debugMode) {
        cout << "\n=== 交易参数诊断 ===\n";
        cout << "B1 (短線): " << B1 << " 天\n";
        cout << "B2 (長線): " << B2 << " 天\n";
        cout << "回測起點 startIdx: " << startIdx << " (" << (startIdx >= 0 && startIdx < (int)g_dates.size() ? g_dates[startIdx] : "無效") << ")\n";
        cout << "回測終點 endIdx: " << endIdx << " (" << (endIdx >= 0 && endIdx < (int)g_dates.size() ? g_dates[endIdx] : "無效") << ")\n";
        cout << "最小需要的預熱天數: " << (max(B1, B2) - 1) << "\n";
        if (startIdx < max(B1, B2) - 1) {
            cout << "提示：預熱期可能不足，初期計算的 MA 會基於可用的歷史數據\n";
        }
        cout << "================================\n\n";
    }

    if (startIdx > endIdx) return -1e18;

    long double cash = 10000.0L, shares = 0.0L;
    bool holding = false;

    for (int closesIdx = startIdx; closesIdx <= endIdx; closesIdx++) {
         // 計算相對於 回測起始日 的交易日序號 (1-indexed)
         // 回測起始日為第 1 個交易日
         int offsetFromStart = closesIdx - g_outputStartIdx + 1;

         // 直接使用 closesIdx 作為 MA 數組的索引
         int maIdxS = closesIdx;
         int maIdxL = closesIdx;

         // 檢查索引有效性
         if (maIdxS < 0 || maIdxL < 0) continue;
         if (maIdxS >= (int)maS.size() || maIdxL >= (int)maL.size()) continue;

         // ✅ 新邏輯：MA 有效性判斷基於 MA 值本身是否有效
         // 只要 MA 值不為 0，就認為有效（不再根據 offsetFromStart 限制）
         bool sValid = (maS[maIdxS] > 0);
         bool lValid = (maL[maIdxL] > 0);

        long double sTody = maS[maIdxS];
        long double lTody = maL[maIdxL];
        long double price = closes[closesIdx];

        // 如果 MA 無效，只記錄不交易
        if (!sValid || !lValid) {
            // 調試輸出：顯示起始日的 MA 計算過程
            if (g_debugMode && closesIdx == g_outputStartIdx) {
                cout << "\n=== 起始日 MA 計算調試 (B1=" << B1 << " B2=" << B2 << ") ===\n";
                cout << "日期: " << g_dates[closesIdx] << " (closesIdx=" << closesIdx << ")\n";
                cout << "收盤價: " << fixed << setprecision(2) << price << "\n";
                cout << "相對偏移: " << offsetFromStart << "\n";

                cout << "\n【短期 MA (B1=" << B1 << " 天)】\n";
                cout << "需要在第 " << B1 << " 個交易日或之後才能有效\n";
                cout << "實際: 第 " << offsetFromStart << " 個交易日\n";
                int dataStart = max(0, closesIdx - B1 + 1);
                int dataEnd = closesIdx;
                cout << "使用範圍: closes[" << dataStart << "] 到 [" << dataEnd << "]\n";
                cout << "日期: " << g_dates[dataStart] << " 到 " << g_dates[dataEnd] << "\n";

                long double sum = 0;
                for (int i = dataStart; i <= dataEnd; i++) {
                    sum += closes[i];
                }
                int numValues = dataEnd - dataStart + 1;
                cout << "數據筆數: " << numValues << " (需要 " << B1 << " 筆)\n";
                cout << "總和: " << fixed << setprecision(2) << sum << "\n";

                if (numValues == B1) {
                    cout << "✅ 手算 MA = " << fixed << setprecision(4) << (sum / B1) << "\n";
                }
                else {
                    cout << "⚠️ 資料不足，手算 MA = " << fixed << setprecision(4) << (sum / numValues) << "\n";
                }
                cout << "程式 maS[" << closesIdx << "] = " << fixed << setprecision(4) << sTody << "\n";
                if (!sValid) {
                    cout << "❌ 短線 MA 無效 (sValid=false)\n";
                }

                cout << "\n【長期 MA (B2=" << B2 << " 天)】\n";
                cout << "需要在第 " << B2 << " 個交易日或之後才能有效\n";
                cout << "實際: 第 " << offsetFromStart << " 個交易日\n";
                dataStart = max(0, closesIdx - B2 + 1);
                dataEnd = closesIdx;
                cout << "使用範圍: closes[" << dataStart << "] 到 [" << dataEnd << "]\n";
                cout << "日期: " << g_dates[dataStart] << " 到 " << g_dates[dataEnd] << "\n";

                sum = 0;
                for (int i = dataStart; i <= dataEnd; i++) {
                    sum += closes[i];
                }
                numValues = dataEnd - dataStart + 1;
                cout << "數據筆數: " << numValues << " (需要 " << B2 << " 筆)\n";
                cout << "總和: " << fixed << setprecision(2) << sum << "\n";

                if (numValues == B2) {
                    cout << "✅ 手算 MA = " << fixed << setprecision(4) << (sum / B2) << "\n";
                }
                else {
                    cout << "⚠️ 資料不足，手算 MA = " << fixed << setprecision(4) << (sum / numValues) << "\n";
                }
                cout << "程式 maL[" << closesIdx << "] = " << fixed << setprecision(4) << lTody << "\n";
                if (!lValid) {
                    cout << "❌ 長線 MA 無效 (lValid=false)\n";
                }
                cout << "================================\n\n";
            }

            if (dailyMA) {
                DailyMARecord daily;
                daily.date = (closesIdx >= 0 && closesIdx < (int)g_dates.size()) ? g_dates[closesIdx] : "";
                daily.price = price;
                daily.shortMA = sTody;
                daily.longMA = lTody;
                daily.isGoldenCross = false;
                daily.isDeathCross = false;
                dailyMA->push_back(daily);
            }
            continue;
        }

        // ✅ MA 都有效的情況：執行交叉判斷
        long double sYest = (maIdxS > 0) ? maS[maIdxS - 1] : 0;
        long double lYest = (maIdxL > 0) ? maL[maIdxL - 1] : 0;

        // 黃金交叉：短線從下方穿過長線
        // 條件：昨天短線 < 長線 且 今天短線 > 長線
        bool isGoldenCross = (sYest - lYest < epsilon) && (sTody - lTody > epsilon);

        // 死亡交叉：短線從上方穿過長線
        // 條件：昨天短線 > 長線 且 今天短線 < 長線
        bool isDeathCross = (sYest - lYest > -epsilon) && (sTody - lTody < -epsilon);

        // 記錄每日 MA 數據（從起始日開始記錄）
        if (dailyMA) {
            DailyMARecord daily;
            daily.date = (closesIdx >= 0 && closesIdx < (int)g_dates.size()) ? g_dates[closesIdx] : "";
            daily.price = price;
            daily.shortMA = sTody;
            daily.longMA = lTody;
            daily.volume = (closesIdx >= 0 && closesIdx < (int)volumes.size()) ? volumes[closesIdx] : 0;
            daily.shortVolumeMA = (closesIdx >= 0 && closesIdx < (int)volmaS.size()) ? volmaS[closesIdx] : 0;
            daily.longVolumeMA = (closesIdx >= 0 && closesIdx < (int)volmaL.size()) ? volmaL[closesIdx] : 0;
            daily.isGoldenCross = isGoldenCross;
            daily.isDeathCross = isDeathCross;
            dailyMA->push_back(daily);
        }

        // 黃金交叉 → 買進
        if (!holding && isGoldenCross) {
            // 預留手續費方式：計算有效股價（包含手續費）
            long double effectivePrice = price * (1.0 + COMMISSION);
            shares = (long double)((long long)(cash / effectivePrice));

            if (shares > 0) {
                long double commission = calcCommission(price, shares);
                long double totalCost = shares * price + commission;
                cash -= totalCost;
                holding = true;

                // 記錄買入交易
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
        // 死亡交叉 → 賣出
        else if (holding && isDeathCross) {
            long double revenue = shares * price;
            long double commission = calcCommission(price, shares);
            cash += revenue - commission;

            // 記錄賣出交易
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
    // 持倉未平：以最後收盤價結算
    if (holding) {
        long double price = closes.back();
        long double revenue = shares * price;
        long double commission = calcCommission(price, shares);
        cash += revenue - commission;

        // 記錄期末平倉交易
        if (trades) {
            TradeRecord rec;
            rec.date = (int)closes.size() - 1 >= 0 && (int)closes.size() - 1 < (int)g_dates.size() ? g_dates[(int)closes.size() - 1] : "";
            rec.action = "期末平倉";
            rec.price = price;
            rec.shares = shares;
            rec.commission = commission;
            rec.cash_after = cash;
            trades->push_back(rec);
        }
    }
    return cash - 10000.0L;
}

// ============================================================
//  初始化：讓 Q[] 產生合理的 B1、B2 初始分布
//  B1 目標範圍 ~5~50  → 初始 Q 使平均值偏向較小數
//  B2 目標範圍 ~20~200 → 初始 Q 使平均值偏向較大數
//  做法：bit 位元的 Q 值設為 0.5（隨機），
//        但確保 B2 編碼的高位元 Q 稍高，讓 B2 > B1 機率大
// ============================================================
void initialization() {
    // 所有位元的 Q 都設為 0.5（完全隨機均勻）
    // B1 和 B2 沒有偏好，允許任意組合
    for (int i = 0; i < NUM_BITS; i++) Q[i] = 0.5;
}

void measure() {
    for (int k = 0; k < NUM_PARTICLES; k++)
        for (int i = 0; i < NUM_BITS; i++)
            particles[k][i] = (Q[i] > rand() / (double)RAND_MAX) ? 1 : 0;
}

void calculation(const vector<long double>& closes) {
    best_fitness = -1e18;
    west_fitness = 1e18;
    for (int k = 0; k < NUM_PARTICLES; k++) {
        int B1 = bitsToInt(particles[k], 0);
        int B2 = bitsToInt(particles[k], 8);
        int V1 = bitsToInt(particles[k], 16);
        int V2 = bitsToInt(particles[k], 24);

        vector<TradeRecord> trades;
        vector<DailyMARecord> dailyMA;
        long double fit = evaluate(B1, B2, V1, V2, closes, g_volumes, &trades, &dailyMA);

        if (fit > best_fitness) {
            best_fitness = fit;
            g_bestTrades = trades;  // 保存最佳解的交易明細
            g_bestDailyMA = dailyMA;  // 保存最佳解的每日 MA 數據
            for (int i = 0; i < NUM_BITS; i++) bestsol[i] = particles[k][i];
        }
        if (fit < west_fitness) {
            west_fitness = fit;
            for (int i = 0; i < NUM_BITS; i++) westsol[i] = particles[k][i];
        }
    }
}

void update() {
    for (int i = 0; i < NUM_BITS; i++) {
        if (bestsol[i] == 1 && westsol[i] == 0) Q[i] += THETA;
        else if (bestsol[i] == 0 && westsol[i] == 1) Q[i] -= THETA;
    }
}

// ============================================================
//  主程式
// ============================================================
int main() {
    srand(114);

    cout << "載入股票資料: " << TICKER << "\n";
    cout << "回測時間範圍: " << BACKTEST_START << " 至 " << BACKTEST_END << "\n";

    // 先顯示 CSV 中的實際日期範圍
    showDateRange(CSV_PATH);

    // QTS優化階段：精確控制預熱天數為 255（支援 B2 最大值 256）
    int warmupDays = 255;
    vector<long double> closes = loadCloses(CSV_PATH, TICKER, BACKTEST_START, BACKTEST_END, g_outputStartIdx, g_outputEndIdx, g_dates, g_volumes, warmupDays);
    cout << "總加載筆數: " << closes.size() << " 筆\n";
    cout << "  - 預熱期數據: " << g_outputStartIdx << " 筆 (用於計算 MA)\n";
    cout << "  - 回測期數據: " << (g_outputEndIdx - g_outputStartIdx + 1) << " 筆\n\n";

    int         gbest_B1 = 0, gbest_B2 = 0, gbest_V1 = 0, gbest_V2 = 0, best_gen = -1;
    long double gbest_fitness = -1e18;

    cout << "世代\tB1\tB2\tV1\tV2\t當代報酬\t\t全局最佳報酬\n";
    cout << string(75, '-') << "\n";

    initialization();
    cout << fixed << setprecision(0);  // 金額格式：無小數位

    // 第一次優化時啟用調試模式
    bool firstTime = true;

    for (int gen = 0; gen < NUM_GENS; gen++) {
        measure();

        // 只在第一代啟用調試（不限於找到新最佳解）
        if (gen == 0) {
            g_debugMode = true;
        }

        calculation(closes);
        g_debugMode = false;  // 後續關閉調試
        firstTime = false;

        update();

        int curB1 = bitsToInt(bestsol, 0);
        int curB2 = bitsToInt(bestsol, 8);
        int curV1 = bitsToInt(bestsol, 16);
        int curV2 = bitsToInt(bestsol, 24);

        if (best_fitness > gbest_fitness) {
            gbest_fitness = best_fitness;
            gbest_B1 = curB1;
            gbest_B2 = curB2;
            gbest_V1 = curV1;
            gbest_V2 = curV2;
            best_gen = gen + 1;
        }

        cout << (gen + 1) << "\t" << curB1 << "\t" << curB2 << "\t" << curV1 << "\t" << curV2
            << "\t" << (double)best_fitness
            << "\t\t" << (double)gbest_fitness << "\n";
    }

    cout << "=== 最終結果 ===\n";
    cout << "最佳 B1 (短期MA): " << gbest_B1 << " 天\n";
    cout << "最佳 B2 (長期MA): " << gbest_B2 << " 天\n";
    cout << "最佳 V1 (成交量短期MA): " << gbest_V1 << " 天\n";
    cout << "最佳 V2 (成交量長期MA): " << gbest_V2 << " 天\n";

    // 警告：檢查短線是否小於長線
    if (gbest_B1 > gbest_B2) {
        cout << "\n⚠️  警告：短期 MA (" << gbest_B1 << ") > 長期 MA (" << gbest_B2 << ")\n";
        cout << "   這可能導致交叉邏輯反向！\n\n";
    }

    cout << "最佳報酬:         " << (double)gbest_fitness << " 元\n";
    cout << "達成世代:         " << best_gen << "\n";

    // ── 起始日 MA 預熱天數說明 ────────────────────────────────
    // B1/B2 的 SMA 計算規則：
    //   起始日（含自身）+ 起始日前 (B-1) 個有交易的日子 = 共 B 筆
    //   所以起始日前需要往回找的交易日數 = B - 1
    int warmupB1 = gbest_B1 - 1;   // 短線起始日前需要的交易日數
    int warmupB2 = gbest_B2 - 1;   // 長線起始日前需要的交易日數
    int warmupNeeded = max(gbest_B1, gbest_B2) - 1;  // 實際需要的預熱天數

    cout << "\n*** 起始日 SMA 預熱天數說明 ***\n";
    cout << "回測起始日: " << BACKTEST_START << "\n";
    cout << "短線 B1=" << gbest_B1 << " → 含起始日共需 " << gbest_B1
        << " 筆，起始日前需往回找 " << warmupB1 << " 個有交易的日子\n";
    cout << "長線 B2=" << gbest_B2 << " → 含起始日共需 " << gbest_B2
        << " 筆，起始日前需往回找 " << warmupB2 << " 個有交易的日子\n";
    cout << "→ 兩者取大值，實際預熱天數 = max(" << gbest_B1 << "," << gbest_B2
        << ") - 1 = " << warmupNeeded << " 個交易日\n";

    // 確認 CSV 預熱期是否足夠
    cout << "\nCSV 實際載入預熱筆數: " << g_outputStartIdx << " 筆";
    if (g_outputStartIdx >= warmupNeeded) {
        cout << "  ✅ 充足（需要 " << warmupNeeded << " 筆）\n";
    }
    else {
        cout << "  ❌ 不足！需要 " << warmupNeeded
            << " 筆，僅有 " << g_outputStartIdx << " 筆\n";
    }

    // 顯示預熱期最後幾筆（即起始日前的資料）
    if (g_outputStartIdx > 0) {
        int showStart = max(0, g_outputStartIdx - warmupNeeded);
        cout << "起始日前的預熱資料（最近 " << warmupNeeded << " 筆）：\n";
        for (int i = showStart; i < g_outputStartIdx; i++) {
            cout << "  [" << (i - showStart + 1) << "] " << g_dates[i]
                << " : " << fixed << setprecision(2) << closes[i] << "\n";
        }
    }
    cout << "回測起始日: " << g_dates[g_outputStartIdx] << "\n";

    // ◆◆◆ 使用 QTS 優化阶段已加載的数据進行最終評估 ◆◆◆
    cout << "\n" << string(70, '=') << "\n";
    cout << "【最終驗證】使用最佳參數進行最終回測\n";
    cout << string(70, '=') << "\n";

    // 清空之前的交易記錄和每日 MA 數據
    g_bestTrades.clear();
    g_bestDailyMA.clear();

    // 直接用最佳參數對 QTS 加載的數據進行最終評估
    // 由於預熱期足夠長（255天），任何 B1/B2/V1/V2 組合的 MA 計算都是準確的
    long double final_fitness = evaluate(gbest_B1, gbest_B2, gbest_V1, gbest_V2, closes, g_volumes, &g_bestTrades, &g_bestDailyMA);

    cout << "\n最終回測報酬: " << fixed << setprecision(0) << (double)final_fitness << " 元\n";
    cout << "（應與 QTS 優化階段的最佳報酬相同）\n";
    if (g_outputStartIdx > 0) {
        int showStart = max(0, g_outputStartIdx - warmupNeeded);
        cout << "起始日前的預熱資料（最近 " << warmupNeeded << " 筆）：\n";
        for (int i = showStart; i < g_outputStartIdx; i++) {
            cout << "  [" << (i - showStart + 1) << "] " << g_dates[i]
                << " : " << fixed << setprecision(2) << closes[i] << "\n";
        }
    }
    cout << "回測起始日: " << g_dates[g_outputStartIdx] << "\n";

    // 輸出最佳解的交易明細
    cout << "\n=== 交易明細 ===\n";
    cout << "序號\t日期\t\t動作\t\t股價\t\t股數\t\t手續費\t\t剩餘現金\n";
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
    cout << "交易次數: " << g_bestTrades.size() << "\n";
    cout << "總手續費: " << fixed << setprecision(2) << totalCommission << " 元\n";
    cout << "初始資金: 10000.00 元\n";
    cout << "最終資產: " << (double)gbest_fitness + 10000.0 << " 元\n";

    // 輸出每日 MA 數據
    cout << "\n=== 每日 MA 數據 ===\n";
    cout << "日期\t\t股價\t\t短線MA\t\t長線MA\t\t成交量\t\t成交量短MA\t成交量長MA\t信號\n";
    cout << string(140, '-') << "\n";

    for (int i = 0; i < (int)g_bestDailyMA.size(); i++) {
        const DailyMARecord& daily = g_bestDailyMA[i];
        string signal = "";
        if (daily.isGoldenCross) signal = "🟢黃金交叉(買入)";
        else if (daily.isDeathCross) signal = "🔴死亡交叉(賣出)";

        cout << daily.date << "\t"
            << fixed << setprecision(2) << daily.price << "\t\t"
            << fixed << setprecision(6) << daily.shortMA << "\t\t"
            << fixed << setprecision(6) << daily.longMA << "\t\t"
            << fixed << setprecision(0) << daily.volume << "\t\t"
            << fixed << setprecision(0) << daily.shortVolumeMA << "\t\t\t"
            << fixed << setprecision(0) << daily.longVolumeMA << "\t\t\t"
            << signal << "\n";
    }

    cout << string(140, '-') << "\n";
    cout << "記錄天數: " << g_bestDailyMA.size() << " 天\n";

    return 0;
}
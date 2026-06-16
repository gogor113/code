//+------------------------------------------------------------------+
//|                                              SmartRiskManager.mq5|
//|                                  Copyright 2026, Gogor   v28.43  |
//|POLA V ZZ - ENTRY REVERSAL SETELAH STRUKTUR V TERKONFIRMASI       |
//|       LOGIKA ANTI-FREEZE & AUTO-RECOVERY SYSTEM (NONSTOP)        |
//+------------------------------------------------------------------+
/* 1 */ #property version   "28.43" // Update: Auto-Healing, Daily Cache Reset, Deep Diagnostics
/* 2 */ #property strict           
/* 3 */ #property copyright "STRATEGY V-PATTERN ZZ V1"
/* 4 */ #property description "SYSTEM SIAGA 24H - AUTO RECOVERY ENABLED"
/* 5 */ #property description "WAJIB UJICOBA DI DEMO_KAMI TIDAK BERTANGGUNGJAWAB ATAS SEGALA KERUGIAN"
/* 6 */ #property description "https://www.facebook.com/andry.tambak "
/* 7 */ #property description "t.me/andrianto13"
/* 8 */ 
/* 9 */ //--- [Parameter Input Operasional] ---
/* 10 */ input double   InpRiskAmount     = 1.0;     // 1. Risiko per transaksi (USD)
/* 11 */ input double   InpMaxRiskUSD     = 1.1;     // 2. Batas floating loss per tiket
/* 12 */ input int      InpMaxSpread      = 500;     // 3. Max Spread allowed
/* 13 */ input double   InpDailyTargetUSD = 5.0;     // 4. Target Profit Harian
/* 14 */ input double   InpDailyLossUSD   = 100.0;   // 5. Batas Loss Harian 
/* 15 */ input double   InpMinEquity      = 5.0;     // 6. Minimal Equity Operasional
/* 16 */ 
/* 17 */ //--- [Parameter Permanen] ---
/* 18 */ const double   InpBufferPercent  = 10.0;    
/* 19 */ const double   InpRewardRatio    = 2.0;     
/* 20 */ const long     InpMagicNumber    = 130806;  
/* 21 */ const bool     InpAutoEntry      = true;    
/* 22 */ 
/* 23 */ const int      InpZZDepth        = 5;       
/* 24 */ const int      InpZZDev          = 5;       
/* 25 */ const int      InpZZStep         = 3;       
/* 26 */ 
/* 27 */ const color    InpMainColor      = clrYellow; 
/* 28 */ const color    InpValColor       = clrAqua;   
/* 29 */ const color    InpPosColor       = clrLime;    
/* 30 */ const color    InpNegColor       = clrRed;     
/* 31 */ 
/* 32 */ #include <Trade\Trade.mqh>           
/* 33 */ #include <Trade\PositionInfo.mqh>    
/* 34 */ 
/* 35 */ //--- Variabel Global ---
/* 36 */ CTrade         trade;                
/* 37 */ CPositionInfo  posInfo;              
/* 38 */ string         prefix = "SRM_Pro_";  
/* 39 */ datetime       EXPIRED_DATE = D'2026.07.27 23:59'; 
/* 40 */ int            hZZ = INVALID_HANDLE;                
/* 41 */ 
/* 42 */ double         lastZZH = 0, lastZZL = 0;            
/* 43 */ datetime       lastEntryZZTimeH = 0;                
/* 44 */ datetime       lastEntryZZTimeL = 0;                
/* 45 */ datetime       lastRefreshTime = 0;                 
/* 46 */ datetime       lastCandleTime = 0;                  
/* 47 */ datetime       lastDailyResetTime = 0; 
/* 48 */ string         g_LastErrorMessage = "System Ready"; 
/* 49 */ bool           g_IsSystemHalted   = false;          
/* 50 */ bool           g_DailyLimitHit    = false;          
/* 51 */ bool           g_MarketClosed     = false;          
/* 52 */ bool           g_LowBalance       = false;          
/* 53 */ 
/* 54 */ //+------------------------------------------------------------------+
/* 55 */ //| OnInit: Inisialisasi Sistem                                      |
/* 56 */ //+------------------------------------------------------------------+
/* 57 */ int OnInit()
/* 58 */ {
/* 59 */    trade.SetExpertMagicNumber(InpMagicNumber); 
/* 60 */    trade.SetTypeFillingBySymbol(_Symbol);      
/* 61 */    
/* 62 */    if(!InitializeZZHandler()) return(INIT_FAILED); 
/* 63 */    
/* 64 */    lastRefreshTime = TimeCurrent(); 
/* 65 */    lastDailyResetTime = iTime(_Symbol, PERIOD_D1, 0); 
/* 66 */    
/* 67 */    ObjectsDeleteAll(0, prefix);     
/* 68 */    CreateDashboard();               
/* 69 */    EventSetTimer(1);                
/* 70 */    return(INIT_SUCCEEDED);          
/* 71 */ }
/* 72 */ 
/* 73 */ //+------------------------------------------------------------------+
/* 74 */ //| InitializeZZHandler: Load Indikator (Auto-Recovery Enabled)      |
/* 75 */ //+------------------------------------------------------------------+
/* 76 */ bool InitializeZZHandler()
/* 77 */ {
/* 78 */    if(hZZ != INVALID_HANDLE) IndicatorRelease(hZZ); 
/* 79 */    hZZ = iCustom(_Symbol, _Period, "Examples\\ZigZag", InpZZDepth, InpZZDev, InpZZStep); 
/* 80 */    
/* 81 */    if(hZZ == INVALID_HANDLE) { 
/* 82 */       g_LastErrorMessage = "CRITICAL: ZZ Handle Failed - Recovering..."; 
/* 83 */       return false; 
/* 84 */    }
/* 85 */    g_LastErrorMessage = "ZZ Engine Synchronized";
/* 86 */    return true; 
/* 87 */ }
/* 88 */ 
/* 89 */ //+------------------------------------------------------------------+
/* 90 */ //| OnDeinit: Cleanup                                                |
/* 91 */ //+------------------------------------------------------------------+
/* 92 */ void OnDeinit(const int reason) 
/* 93 */ { 
/* 94 */    EventKillTimer(); 
/* 95 */    if(hZZ != INVALID_HANDLE) IndicatorRelease(hZZ); 
/* 96 */    ObjectsDeleteAll(0, prefix); 
/* 97 */ }
/* 98 */ 
/* 99 */ //+------------------------------------------------------------------+
/* 100 */ //| OnTick: Logika Utama (Anti-Freeze & Continuous Monitoring)       |
/* 101 */ //+------------------------------------------------------------------+
/* 102 */ void OnTick()
/* 103 */ {
/* 104 */    // 1. Force System Healing (Auto-Correct All Errors)
/* 105 */    ForceSystemHealing();
/* 106 */ 
/* 107 */    // 2. Housekeeping: Reset data harian (Tanpa Tutup Posisi)
/* 108 */    CheckAccountAndMarket(); 
/* 109 */    MonitorAndResetSystem(); 
/* 110 */    
/* 111 */    if(g_IsSystemHalted) return; 
/* 112 */    if(!TerminalInfoInteger(TERMINAL_CONNECTED)) { g_LastErrorMessage = "OFFLINE: Reconnecting..."; return; }
/* 113 */ 
/* 114 */    // 3. Profit/Loss Tracking
/* 115 */    double dailyProfit = GetDailyRealizedProfit(); 
/* 116 */    g_DailyLimitHit = (dailyProfit >= InpDailyTargetUSD || dailyProfit <= -InpDailyLossUSD); 
/* 117 */ 
/* 118 */    // 4. Integrity & Protection
/* 119 */    AntiManualOrderFilter();     
/* 120 */    RestoreIntegritySLTP();      
/* 121 */    CheckSlippageAndProtection(); 
/* 122 */    
/* 123 */    // 5. Signal Update & Continuous Scan
/* 124 */    datetime currentBarTime = iTime(_Symbol, _Period, 0); 
/* 125 */    if(currentBarTime != lastCandleTime) { 
/* 126 */       SyncToLatestZZ(); 
/* 127 */       lastCandleTime = currentBarTime; 
/* 128 */    }
/* 129 */    
/* 130 */    // 6. Execution Logic
/* 131 */    if(InpAutoEntry && !g_DailyLimitHit && !g_MarketClosed && !g_LowBalance && !g_IsSystemHalted) { 
/* 132 */       if(MQLInfoInteger(MQL_TRADE_ALLOWED) && AccountInfoInteger(ACCOUNT_TRADE_ALLOWED)) {
/* 133 */          ExecuteMarketZZ(); 
/* 134 */       } else {
/* 135 */          g_LastErrorMessage = "WAIT: Trade Permission Denied";
/* 136 */       }
/* 137 */    }
/* 138 */    
/* 139 */    UpdateDashboard(); 
/* 140 */ }
/* 141 */ 
/* 142 */ //+------------------------------------------------------------------+
/* 143 */ //| ForceSystemHealing: Fitur Anti Bug, Error, & Freeze              |
/* 144 */ //+------------------------------------------------------------------+
/* 145 */ void ForceSystemHealing()
/* 146 */ {
/* 147 */    // Deteksi ZZ Freeze
/* 148 */    if(hZZ == INVALID_HANDLE) {
/* 149 */       g_LastErrorMessage = "HEALING: Re-initializing Indicators...";
/* 150 */       InitializeZZHandler();
/* 151 */    }
/* 152 */    
/* 153 */    // Deteksi Sinkronisasi Chart
/* 154 */    if(iTime(_Symbol, _Period, 0) == 0) {
/* 155 */       g_LastErrorMessage = "HEALING: Syncing Price Data...";
/* 156 */       return;
/* 157 */    }
/* 158 */ 
/* 159 */    // Deteksi Trade Context Busy
/* 160 */    if(!IsStopped() && !TerminalInfoInteger(TERMINAL_CONNECTED)) {
/* 161 */       g_LastErrorMessage = "HEALING: Waiting for Connection...";
/* 162 */    }
/* 163 */ }
/* 164 */ 
/* 165 */ //+------------------------------------------------------------------+
/* 166 */ //| MonitorAndResetSystem: Riset Harian & Cache Clearance            |
/* 167 */ //+------------------------------------------------------------------+
/* 168 */ void MonitorAndResetSystem()
/* 169 */ {
/* 170 */    datetime now = TimeCurrent(); 
/* 171 */    datetime currentDayStart = iTime(_Symbol, PERIOD_D1, 0);
/* 172 */ 
/* 173 */    // --- FITUR AUTO RESET HARI BARU (MODE RISET) ---
/* 174 */    if(currentDayStart > lastDailyResetTime) {
/* 175 */       lastEntryZZTimeH = 0; 
/* 176 */       lastEntryZZTimeL = 0; 
/* 177 */       lastDailyResetTime = currentDayStart;
/* 178 */       
/* 179 */       // Hapus cache tanpa menutup posisi aktif
/* 180 */       g_LastErrorMessage = "NEW DAY RESET: Scanning New Opportunities";
/* 181 */       
/* 182 */       InitializeZZHandler(); // Refresh handle untuk hari baru
/* 183 */       ChartRedraw(0);
/* 184 */    }
/* 185 */ 
/* 186 */    // Anti-Freeze: Reset paksa logika stagnan
/* 187 */    if(now - lastRefreshTime >= 3600) { 
/* 188 */       if(GetActivePositionsCount() == 0) {
/* 189 */          lastEntryZZTimeH = 0; 
/* 190 */          lastEntryZZTimeL = 0;
/* 191 */          g_LastErrorMessage = "HOURLY HEAL: Logic Refreshed";
/* 192 */       }
/* 193 */       lastRefreshTime = now; 
/* 194 */    }
/* 195 */ }
/* 196 */ 
/* 197 */ //+------------------------------------------------------------------+
/* 198 */ //| ExecuteMarketZZ: Logika Pola V (Detailed Last Msg)               |
/* 199 */ //+------------------------------------------------------------------+
/* 200 */ void ExecuteMarketZZ()
/* 201 */ {
/* 202 */    int currentSpread = (int)SymbolInfoInteger(_Symbol, SYMBOL_SPREAD); 
/* 203 */    if(currentSpread > InpMaxSpread || currentSpread < 0) {
/* 204 */       g_LastErrorMessage = "FILT: High Spread [" + (string)currentSpread + " > " + (string)InpMaxSpread + "]";
/* 205 */       return; 
/* 206 */    }
/* 207 */ 
/* 208 */    double zzH, zzL; datetime tH, tL; 
/* 209 */    GetLatestZZData(zzH, zzL, tH, tL); 
/* 210 */    
/* 211 */    if(zzH <= 0 || zzL <= 0) {
/* 212 */       g_LastErrorMessage = "SCAN: Waiting for Structure Formation...";
/* 213 */       return; 
/* 214 */    }
/* 215 */ 
/* 216 */    if(tH >= iTime(_Symbol, _Period, 0) || tL >= iTime(_Symbol, _Period, 0)) {
/* 217 */       g_LastErrorMessage = "SCAN: ZZ Dynamic - Waiting Confirmation";
/* 218 */       return; 
/* 219 */    }
/* 220 */ 
/* 221 */    double priceClose0 = iClose(_Symbol, _Period, 0); 
/* 222 */    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK); 
/* 223 */    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID); 
/* 224 */    int dig = (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS); 
/* 225 */    
/* 226 */    // --- LOGIKA ENTRY BUY ---
/* 227 */    if(priceClose0 > zzH && tH != lastEntryZZTimeH) { 
/* 228 */       double sl = NormalizeDouble(zzL - (MathAbs(zzH-zzL)*(InpBufferPercent/100.0)), dig); 
/* 229 */       double tp = NormalizeDouble(ask + (MathAbs(ask-sl)*InpRewardRatio), dig); 
/* 230 */       double lot = CalculatePreciseLot(ask, sl); 
/* 231 */ 
/* 232 */       if(lot >= SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN) && CheckMarginSafe(ORDER_TYPE_BUY, lot)) { 
/* 233 */          if(trade.Buy(lot, _Symbol, ask, sl, tp, "ZZ_V_B_v28.43")) { 
/* 234 */             lastEntryZZTimeH = tH; 
/* 235 */             g_LastErrorMessage = "EXEC: Buy Confirm @ " + DoubleToString(ask, dig); 
/* 236 */          } else {
/* 237 */             g_LastErrorMessage = "ERR: Buy Fail [" + trade.ResultRetcodeDescription() + "]";
/* 238 */          }
/* 239 */       } else {
/* 240 */          g_LastErrorMessage = "WARN: Lot/Margin Insufficient for Buy";
/* 241 */       }
/* 242 */    }
/* 243 */    
/* 244 */    // --- LOGIKA ENTRY SELL ---
/* 245 */    else if(priceClose0 < zzL && tL != lastEntryZZTimeL) { 
/* 246 */       double sl = NormalizeDouble(zzH + (MathAbs(zzH-zzL)*(InpBufferPercent/100.0)), dig); 
/* 247 */       double tp = NormalizeDouble(bid - (MathAbs(sl-bid)*InpRewardRatio), dig); 
/* 248 */       double lot = CalculatePreciseLot(bid, sl); 
/* 249 */ 
/* 250 */       if(lot >= SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN) && CheckMarginSafe(ORDER_TYPE_SELL, lot)) { 
/* 251 */          if(trade.Sell(lot, _Symbol, bid, sl, tp, "ZZ_V_S_v28.43")) { 
/* 252 */             lastEntryZZTimeL = tL; 
/* 253 */             g_LastErrorMessage = "EXEC: Sell Confirm @ " + DoubleToString(bid, dig); 
/* 254 */          } else {
/* 255 */             g_LastErrorMessage = "ERR: Sell Fail [" + trade.ResultRetcodeDescription() + "]";
/* 256 */          }
/* 257 */       } else {
/* 258 */          g_LastErrorMessage = "WARN: Lot/Margin Insufficient for Sell";
/* 259 */       }
/* 260 */    } else {
/* 261 */       if(GetActivePositionsCount() == 0) {
/* 262 */          g_LastErrorMessage = "SCAN: Searching Opp. [H:"+DoubleToString(zzH,dig)+" L:"+DoubleToString(zzL,dig)+"]";
/* 263 */       }
/* 264 */    }
/* 265 */ }
/* 266 */ 
/* 267 */ //+------------------------------------------------------------------+
/* 268 */ //| GetLatestZZData: Mencari Titik ZZ Terakhir di Buffer             |
/* 269 */ //+------------------------------------------------------------------+
/* 270 */ void GetLatestZZData(double &zzH, double &zzL, datetime &timeH, datetime &timeL) {
/* 271 */    double zzBuf[]; ArraySetAsSeries(zzBuf, true); 
/* 272 */    zzH = 0; zzL = 0; timeH = 0; timeL = 0; 
/* 273 */    int copied = CopyBuffer(hZZ, 0, 0, 500, zzBuf); 
/* 274 */    if(copied > 0) { 
/* 275 */       for(int i = 1; i < copied; i++) { 
/* 276 */          if(zzBuf[i] > 0 && zzBuf[i] < EMPTY_VALUE) { 
/* 277 */             if(MathAbs(zzBuf[i] - iHigh(_Symbol, _Period, i)) < _Point * 10) { 
/* 278 */                zzH = zzBuf[i]; timeH = iTime(_Symbol, _Period, i); break; 
/* 279 */             }
/* 280 */          }
/* 281 */       }
/* 282 */       for(int i = 1; i < copied; i++) { 
/* 283 */          if(zzBuf[i] > 0 && zzBuf[i] < EMPTY_VALUE) { 
/* 284 */             if(MathAbs(zzBuf[i] - iLow(_Symbol, _Period, i)) < _Point * 10) { 
/* 285 */                zzL = zzBuf[i]; timeL = iTime(_Symbol, _Period, i); break; 
/* 286 */             }
/* 287 */          }
/* 288 */       }
/* 289 */    }
/* 290 */    ArrayFree(zzBuf); 
/* 291 */ }
/* 292 */ 
/* 293 */ //+------------------------------------------------------------------+
/* 294 */ //| RestoreIntegritySLTP: Paksa Keamanan Transaksi                   |
/* 295 */ //+------------------------------------------------------------------+
/* 296 */ void RestoreIntegritySLTP() {
/* 297 */    if(!MQLInfoInteger(MQL_TRADE_ALLOWED)) return;
/* 298 */    for(int i = PositionsTotal() - 1; i >= 0; i--) { 
/* 299 */       if(posInfo.SelectByIndex(i) && posInfo.Symbol() == _Symbol && posInfo.Magic() == InpMagicNumber) { 
/* 300 */          int dig = (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS); 
/* 301 */          if(posInfo.StopLoss() <= 0 || posInfo.TakeProfit() <= 0) { 
/* 302 */             double zzH, zzL; datetime tH, tL; GetLatestZZData(zzH, zzL, tH, tL); 
/* 303 */             if(zzH <= 0 || zzL <= 0) continue; 
/* 304 */             double targetSL = 0, targetTP = 0; 
/* 305 */             if(posInfo.PositionType() == POSITION_TYPE_BUY) { 
/* 306 */                targetSL = NormalizeDouble(zzL - (MathAbs(zzH-zzL)*(InpBufferPercent/100.0)), dig); 
/* 307 */                targetTP = NormalizeDouble(posInfo.PriceOpen() + (MathAbs(posInfo.PriceOpen()-targetSL)*InpRewardRatio), dig); 
/* 308 */             } else { 
/* 309 */                targetSL = NormalizeDouble(zzH + (MathAbs(zzH-zzL)*(InpBufferPercent/100.0)), dig); 
/* 310 */                targetTP = NormalizeDouble(posInfo.PriceOpen() - (MathAbs(targetSL-posInfo.PriceOpen())*InpRewardRatio), dig); 
/* 311 */             }
/* 312 */             trade.PositionModify(posInfo.Ticket(), targetSL, targetTP); 
/* 313 */             g_LastErrorMessage = "HEAL: SL/TP Integrity Restored";
/* 314 */          }
/* 315 */       }
/* 316 */    }
/* 317 */ }
/* 318 */ 
/* 319 */ //+------------------------------------------------------------------+
/* 320 */ //| Kalkulasi & Filter Pendukung                                     |
/* 321 */ //+------------------------------------------------------------------+
/* 322 */ double CalculatePreciseLot(double entry, double sl) {
/* 323 */    double dist = MathAbs(entry - sl); if(dist < _Point) return SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN); 
/* 324 */    double tv = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_TICK_VALUE), ts = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_TICK_SIZE);  
/* 325 */    if(ts <= 0 || tv <= 0) return SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN); 
/* 326 */    double rawLot = InpRiskAmount / ((dist / ts) * tv); 
/* 327 */    double step = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP), minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN), maxLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX); 
/* 328 */    double finalLot = NormalizeDouble(MathMax(MathFloor(rawLot / step) * step, minLot), 2); 
/* 329 */    return (finalLot > maxLot) ? maxLot : finalLot; 
/* 330 */ }
/* 331 */ 
/* 332 */ bool CheckMarginSafe(ENUM_ORDER_TYPE type, double lot) {
/* 333 */    double marginReq = 0; if(!OrderCalcMargin(type, _Symbol, lot, SymbolInfoDouble(_Symbol, (type == ORDER_TYPE_BUY ? SYMBOL_ASK : SYMBOL_BID)), marginReq)) return false; 
/* 334 */    return (AccountInfoDouble(ACCOUNT_MARGIN_FREE) > marginReq); 
/* 335 */ }
/* 336 */ 
/* 337 */ void SyncToLatestZZ() { double zzH, zzL; datetime tH, tL; GetLatestZZData(zzH, zzL, tH, tL); lastZZH = zzH; lastZZL = zzL; }
/* 338 */ 
/* 339 */ void AntiManualOrderFilter() {
/* 340 */    for(int i = PositionsTotal() - 1; i >= 0; i--) if(posInfo.SelectByIndex(i) && posInfo.Symbol() == _Symbol && posInfo.Magic() != InpMagicNumber) trade.PositionClose(posInfo.Ticket()); 
/* 341 */    for(int i = OrdersTotal() - 1; i >= 0; i--) { ulong t = OrderGetTicket(i); if(OrderSelect(t) && OrderGetInteger(ORDER_MAGIC) != InpMagicNumber) trade.OrderDelete(t); } 
/* 342 */ }
/* 343 */ 
/* 344 */ double GetDailyRealizedProfit() {
/* 345 */    double p = 0; HistorySelect(iTime(_Symbol, PERIOD_D1, 0), TimeCurrent()); 
/* 346 */    for(int i=HistoryDealsTotal()-1; i>=0; i--) { 
/* 347 */       ulong t = HistoryDealGetTicket(i); if(HistoryDealGetInteger(t, DEAL_MAGIC) == InpMagicNumber && HistoryDealGetInteger(t, DEAL_ENTRY) == DEAL_ENTRY_OUT) p += HistoryDealGetDouble(t, DEAL_PROFIT) + HistoryDealGetDouble(t, DEAL_COMMISSION) + HistoryDealGetDouble(t, DEAL_SWAP); 
/* 348 */    }
/* 349 */    return p; 
/* 350 */ }
/* 351 */ 
/* 352 */ double GetDailyWinRate() {
/* 353 */    int total = 0, win = 0; HistorySelect(iTime(_Symbol, PERIOD_D1, 0), TimeCurrent()); 
/* 354 */    for(int i=HistoryDealsTotal()-1; i>=0; i--) { 
/* 355 */       ulong t = HistoryDealGetTicket(i); if(HistoryDealGetInteger(t, DEAL_MAGIC) == InpMagicNumber && HistoryDealGetInteger(t, DEAL_ENTRY) == DEAL_ENTRY_OUT) { total++; if(HistoryDealGetDouble(t, DEAL_PROFIT) > 0) win++; }
/* 356 */    }
/* 357 */    return (total > 0) ? NormalizeDouble(((double)win/(double)total)*100.0, 1) : 0.0; 
/* 358 */ }
/* 359 */ 
/* 360 */ void CheckAccountAndMarket() {
/* 361 */    if(AccountInfoDouble(ACCOUNT_EQUITY) < InpMinEquity) { g_LowBalance = true; g_LastErrorMessage = "LIMIT: Equity Low [" + DoubleToString(AccountInfoDouble(ACCOUNT_EQUITY),2) + "]"; } else g_LowBalance = false; 
/* 362 */    g_MarketClosed = (SymbolInfoInteger(_Symbol, SYMBOL_TRADE_MODE) == SYMBOL_TRADE_MODE_DISABLED); 
/* 363 */    if(g_MarketClosed) g_LastErrorMessage = "HALT: Market Session Closed";
/* 364 */ }
/* 365 */ 
/* 366 */ void CheckSlippageAndProtection() {
/* 367 */    for(int i = PositionsTotal() - 1; i >= 0; i--) if(posInfo.SelectByIndex(i) && posInfo.Symbol() == _Symbol && posInfo.Magic() == InpMagicNumber) {
/* 368 */       if((posInfo.Profit() + posInfo.Swap() + posInfo.Commission()) <= -InpMaxRiskUSD) {
/* 369 */          trade.PositionClose(posInfo.Ticket()); 
/* 370 */          g_LastErrorMessage = "PROT: Max Risk Ticket #" + (string)posInfo.Ticket() + " Closed";
/* 371 */       }
/* 372 */    }
/* 373 */ }
/* 374 */ 
/* 375 */ int GetActivePositionsCount() {
/* 376 */    int c=0; for(int i=PositionsTotal()-1; i>=0; i--) if(posInfo.SelectByIndex(i) && posInfo.Symbol()==_Symbol && posInfo.Magic()==InpMagicNumber) c++; return c; 
/* 377 */ }
/* 378 */ 
/* 379 */ //+------------------------------------------------------------------+
/* 380 */ //| Dashboard UI System                                              |
/* 381 */ //+------------------------------------------------------------------+
/* 382 */ void OnTimer() { UpdateDashboard(); } 
/* 383 */ 
/* 384 */ void UpdateDashboard() {
/* 385 */    datetime cur = TimeCurrent(); double dPL = GetDailyRealizedProfit(), dWR = GetDailyWinRate(); 
/* 386 */    int aPos = GetActivePositionsCount();
/* 387 */    int currentSpread = (int)SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
/* 388 */ 
/* 389 */    UpdateLabel("ValTime", ": " + TimeToString(cur, TIME_SECONDS)); 
/* 390 */    UpdateLabel("ValActive", ": " + IntegerToString(aPos), (aPos > 0 ? InpPosColor : clrWhite)); 
/* 391 */    UpdateLabel("ValExp", ": " + TimeToString(EXPIRED_DATE, TIME_DATE)); 
/* 392 */    if(cur >= EXPIRED_DATE) { g_IsSystemHalted = true; g_LastErrorMessage = "STOP: License Expired"; } 
/* 393 */    
/* 394 */    color sCol = clrLime; string sTxt = ": ACTIVE"; 
/* 395 */    if(g_IsSystemHalted) { sCol = clrRed; sTxt = ": HALTED"; } 
/* 396 */    else if(g_DailyLimitHit) { sCol = clrOrange; sTxt = ": TARGET REACHED"; } 
/* 397 */    else if(g_LowBalance) { sCol = clrRed; sTxt = ": LOW BALANCE"; } 
/* 398 */    else if(g_MarketClosed) { sCol = clrYellow; sTxt = ": MARKET CLOSED"; } 
/* 399 */    
/* 400 */    UpdateLabel("ValStat", sTxt, sCol); 
/* 401 */    UpdateLabel("ValErr", ": " + g_LastErrorMessage, (g_IsSystemHalted ? clrRed : InpMainColor)); 
/* 402 */    UpdateLabel("ValDaily", ": " + DoubleToString(dPL, 2) + " USD", (dPL >= 0 ? clrLime : clrRed)); 
/* 403 */    UpdateLabel("ValWR", ": " + DoubleToString(dWR, 1) + " %", (dWR >= 50.0 ? clrLime : clrYellow)); 
/* 404 */    UpdateLabel("ValBal", ": " + DoubleToString(AccountInfoDouble(ACCOUNT_BALANCE), 2)); 
/* 405 */    UpdateLabel("ValEqt", ": " + DoubleToString(AccountInfoDouble(ACCOUNT_EQUITY), 2)); 
/* 406 */ 
/* 407 */    UpdateLabel("ValTargetP", ": " + DoubleToString(InpDailyTargetUSD, 2) + " USD", clrLime); 
/* 408 */    UpdateLabel("ValTargetL", ": " + DoubleToString(InpDailyLossUSD, 2) + " USD", clrRed); 
/* 409 */    UpdateLabel("ValSpread", ": " + IntegerToString(currentSpread), (currentSpread <= InpMaxSpread ? clrAqua : clrRed)); 
/* 410 */ 
/* 411 */    double tPL = 0; 
/* 412 */    for(int i = PositionsTotal()-1; i >= 0; i--) if(posInfo.SelectByIndex(i) && posInfo.Magic() == InpMagicNumber) tPL += posInfo.Profit() + posInfo.Swap() + posInfo.Commission(); 
/* 413 */    UpdateLabel("ValPL", DoubleToString(tPL, 2) + " USD", (tPL >= 0 ? InpPosColor : InpNegColor)); 
/* 414 */ }
/* 415 */ 
/* 416 */ void CreateLabel(string n, string t, int x, int y, color c, int s, string f="Arial") {
/* 417 */    if(ObjectFind(0, prefix+n) < 0) ObjectCreate(0, prefix+n, OBJ_LABEL, 0, 0, 0); 
/* 418 */    ObjectSetInteger(0, prefix+n, OBJPROP_XDISTANCE, x); ObjectSetInteger(0, prefix+n, OBJPROP_YDISTANCE, y); 
/* 419 */    ObjectSetInteger(0, prefix+n, OBJPROP_COLOR, c); ObjectSetInteger(0, prefix+n, OBJPROP_FONTSIZE, s); 
/* 420 */    ObjectSetString(0, prefix+n, OBJPROP_TEXT, t); ObjectSetString(0, prefix+n, OBJPROP_FONT, f); 
/* 421 */    ObjectSetInteger(0, prefix+n, OBJPROP_SELECTABLE, false); 
/* 422 */ }
/* 423 */ 
/* 424 */ void UpdateLabel(string n, string t, color c = clrNONE) { if(ObjectFind(0, prefix+n) >= 0) { ObjectSetString(0, prefix+n, OBJPROP_TEXT, t); if(c != clrNONE) ObjectSetInteger(0, prefix+n, OBJPROP_COLOR, c); } }
/* 425 */ 
/* 426 */ void CreateDashboard() {
/* 427 */    int x=20, y=30, s=22, col=120; 
/* 428 */    CreateLabel("Title", " GOGOR V28.43  V ", x, y, InpMainColor, 12, "Arial Black"); y+=40; 
/* 429 */    
/* 430 */    CreateLabel("CatHlt", "AUTONOMOUS MONITOR", x, y, InpMainColor, 9, "Arial Bold"); 
/* 431 */    CreateLabel("ValTime", ": 00:00:00", x+col, y+s, clrAqua, 10); CreateLabel("LblTime", "Server", x, y+s, clrWhite, 10); 
/* 432 */    CreateLabel("ValStat", ": Active", x+col, y+(s*2), clrLime, 10); CreateLabel("LblStat", "Status", x, y+(s*2), clrWhite, 10); 
/* 433 */    CreateLabel("ValExp", ": 00.00.00", x+col, y+(s*3), clrYellow, 10); CreateLabel("LblExp", "Expired", x, y+(s*3), clrWhite, 10); 
/* 434 */    CreateLabel("ValErr", ": System OK", x+col, y+(s*4), clrGray, 9); CreateLabel("LblErr", "Last Msg", x, y+(s*4), clrWhite, 10); 
/* 435 */    CreateLabel("ValActive", ": 0", x+col, y+(s*5), clrYellow, 10); CreateLabel("LblActive", "P Active", x, y+(s*5), clrWhite, 10); 
/* 436 */    
/* 437 */    y+=(s*7); CreateLabel("Cat1", "ACCOUNT METRICS", x, y, InpMainColor, 9, "Arial Bold"); 
/* 438 */    CreateLabel("ValDaily", ": 0.00 USD", x+col, y+s, InpValColor, 10); CreateLabel("LblDaily", "Daily P/L", x, y+s, clrWhite, 10); 
/* 439 */    CreateLabel("ValWR", ": 0.0 %", x+col, y+(s*2), InpValColor, 10); CreateLabel("LblWR", "Daily WR", x, y+(s*2), clrWhite, 10); 
/* 440 */    CreateLabel("ValBal", ": 0.00", x+col, y+(s*3), InpValColor, 10); CreateLabel("LblBal", "Balance", x, y+(s*3), clrWhite, 10); 
/* 441 */    CreateLabel("ValEqt", ": 0.00", x+col, y+(s*4), InpValColor, 10); CreateLabel("LblEqt", "Equity", x, y+(s*4), clrWhite, 10); 
/* 442 */    
/* 443 */    y+=(s*6); CreateLabel("CatSig", "STRUCTURE STATUS", x, y, InpMainColor, 10, "Arial Bold"); 
/* 444 */    CreateLabel("ValTargetP", ": 0.00 USD", x+col, y+s, clrLime, 10); CreateLabel("LblTargetP", "T Profit", x, y+s, clrWhite, 10); 
/* 445 */    CreateLabel("ValTargetL", ": 0.00 USD", x+col, y+(s*2), clrRed, 10); CreateLabel("LblTargetL", "T Lose", x, y+(s*2), clrWhite, 10); 
/* 446 */    CreateLabel("ValSpread", ": 0", x+col, y+(s*3), clrAqua, 10); CreateLabel("LblSpread", "Spread", x, y+(s*3), clrWhite, 10); 
/* 447 */    
/* 448 */    y+=(s*5); CreateLabel("Cat2", "PERFORMANCE", x, y, InpMainColor, 10, "Arial Bold"); 
/* 449 */    CreateLabel("ValPL", "0.00 USD", x, y+s, clrWhite, 16, "Impact"); 
/* 450 */    
/* 451 */    if(ObjectFind(0, prefix+"BtnClose") < 0) { 
/* 452 */       ObjectCreate(0, prefix+"BtnClose", OBJ_BUTTON, 0, 0, 0); 
/* 453 */       ObjectSetInteger(0, prefix+"BtnClose", OBJPROP_XDISTANCE, x); ObjectSetInteger(0, prefix+"BtnClose", OBJPROP_YDISTANCE, y+(s*4));
/* 454 */       ObjectSetInteger(0, prefix+"BtnClose", OBJPROP_XSIZE, 220); ObjectSetInteger(0, prefix+"BtnClose", OBJPROP_YSIZE, 35);
/* 455 */       ObjectSetString(0, prefix+"BtnClose", OBJPROP_TEXT, "CLOSE ALL");
/* 456 */       ObjectSetInteger(0, prefix+"BtnClose", OBJPROP_BGCOLOR, clrFireBrick); ObjectSetInteger(0, prefix+"BtnClose", OBJPROP_COLOR, clrWhite);
/* 457 */    }
/* 458 */ }
/* 459 */ 
/* 460 */ void OnChartEvent(const int id, const long &lparam, const double &dparam, const string &sparam) {
/* 461 */    if(id == CHARTEVENT_OBJECT_CLICK && sparam == prefix+"BtnClose") { 
/* 462 */       for(int i = PositionsTotal()-1; i >= 0; i--) if(posInfo.SelectByIndex(i) && posInfo.Symbol() == _Symbol && posInfo.Magic() == InpMagicNumber) trade.PositionClose(posInfo.Ticket()); 
/* 463 */       ObjectSetInteger(0, prefix+"BtnClose", OBJPROP_STATE, false); 
/* 464 */       lastEntryZZTimeH = 0; lastEntryZZTimeL = 0; 
/* 465 */       g_LastErrorMessage = "MANUAL: Emergency Reset & Cache Purge";
/* 466 */    }
/* 467 */ }

//+------------------------------------------------------------------+
//| GOGOR V12.24                                                     |
//| Hybrid Layered Grid Strategy: Linear -> Pure Linear -> Exp       |
//| STATUS: PROFESSIONAL - READY TO USE                              |
//+------------------------------------------------------------------+
#property copyright "Telegram @andrianto13"
#property version   "12.24"
#property description "HYBRID GRID LAYERING STRATEGY"
#property description "TEST ON DEMO FIRST!"
#property strict

#include <Trade\Trade.mqh>
#include <Trade\HistoryOrderInfo.mqh>

//--- ENUM TRADE MODE
enum ENUM_TRADE_MODE {
   MODE_BOTH = 0,      // Buy and Sell (EMA & Candle Filter Active)
   MODE_BUY_ONLY = 1,  // Buy Only (Bypass all filters)
   MODE_SELL_ONLY = 2  // Sell Only (Bypass all filters)
};

//--- INTERNAL SECURITY
const datetime INTERNAL_EXPIRY = D'2027.12.28 23:59';
const int MAGIC_NUMBER = 109131995;

//--- INPUT PARAMETERS
input group "== MAIN SETTINGS =="
input ENUM_TRADE_MODE Trade_Mode = MODE_BOTH;
input double InitialLot = 0.01;
input double LotStep = 0.01;
input int OrdersPerStep = 33;
input int EMAPeriod = 1;

input group "== PRICE TRIGGER & AUTO SWITCH =="
input double Trigger_CloseAll_Price = 0.0;
input double Auto_Switch_Price = 0.0;
input ENUM_TRADE_MODE Target_Mode_After_Switch = MODE_BOTH;

input group "== PURE LINEAR SETTINGS (PHASE 2) =="
input int PureLinearStep = 155;
input double PureLinearLotAdd = 0.02;
input int PureLinearInterval = 33;

input group "== GRID LOGIC =="
input int StepPoints = 1111;
input int AntiStackDist = 1100;
input int MaxSpread = 500;

input group "== DYNAMIC TRAILING =="
input int TrailingStartPoints = 4444;
input int TrailingStepPoints = 1111;

input group "== CYCLE PROTECTION =="
input double CutlossUSD = 55000.00;
input double DailyTargetUSD = 55000.00;
input bool AntiManual = true;

input group "== RESOURCE MANAGEMENT =="
input bool AutoPurgeLog = true;

input group "== HYBRID RECOVERY (PHASE 3) =="
input int RecoveryThreshold = 555;
input int RecoveryLayerStep = 33;
input double ExpMultiplier = 2.0;
input double MaxLotLimit = 3.0;

//--- GLOBAL VARIABLES
CTrade trade;
int handleEMA = INVALID_HANDLE;
bool cycleCompleted = false;
bool isExpired = false;
double startBalance = 0;
string btnCloseName = "GGR_BTN_CLOSE_ALL";
string btnModeBoth = "GGR_BTN_MODE_BOTH";
string btnModeBuy = "GGR_BTN_MODE_BUY";
string btnModeSell = "GGR_BTN_MODE_SELL";
datetime lastTickTime = 0;
int lastPositionCount = 0;
bool isModeSwitched = false;
double LastKnownPrice = 0;
ENUM_TRADE_MODE currentTradeMode;

//--- FUNCTION PROTOTYPES
int CountPositions();
void ExecuteCloseAllStrategy();
void ScanAndKillManualOrders();
double CalculateTotalProfit();
void CountTypePositions(int &bC, int &sC, double &bL, double &sL);
void CheckLayeringByType(ENUM_POSITION_TYPE type, int count, MqlTick &t, int oppositeCount);
bool CalibratedExecute(ENUM_ORDER_TYPE type, double lot, string comment);
double GetEMAValue();
bool IsCandleConfirm(ENUM_ORDER_TYPE type);
void ManageLatestPositionTrailing(MqlTick &t);
bool GetLastPriceAndLotByType(ENUM_POSITION_TYPE type, double &price, double &lot);
double CalculateHybridLot(int count, double lastLot);
bool IsMarginEnough(double lot, ENUM_ORDER_TYPE orderType);
bool IsPriceTooClose(double price, int dist);
void CleanTrashLogs();
void SetTxt(string n, string t, int x, int y, color c, int s, string font="Arial");
void DrawLine(string n, int x, int y, int w, color c);
void SetBtn(string n, string t, int x, int y, int w, int h, color bg, color tc);
void UpdateDashboardData();

//+------------------------------------------------------------------+
//| OnInit Initialization                                           |
//+------------------------------------------------------------------+
int OnInit() {
   if(TimeCurrent() >= INTERNAL_EXPIRY) {
      isExpired = true;
      Print("GGR: SYSTEM EXPIRED.");
      return(INIT_FAILED);
   }
   
   cycleCompleted = false;
   startBalance = AccountInfoDouble(ACCOUNT_BALANCE);
   lastPositionCount = CountPositions();
   isModeSwitched = false;
   currentTradeMode = Trade_Mode;
   
   handleEMA = iMA(_Symbol, _Period, EMAPeriod, 0, MODE_EMA, PRICE_CLOSE);
   if(handleEMA == INVALID_HANDLE) return(INIT_FAILED);
   
   trade.SetExpertMagicNumber(MAGIC_NUMBER);
   trade.SetDeviationInPoints(100);
   
   EventSetTimer(1);
   return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| OnDeinit Cleanup                                                |
//+------------------------------------------------------------------+
void OnDeinit(const int reason) {
   if(handleEMA != INVALID_HANDLE) IndicatorRelease(handleEMA);
   ObjectsDeleteAll(0, "GGR_");
   Comment("");
   EventKillTimer();
}

//+------------------------------------------------------------------+
//| OnTick Main Logic                                               |
//+------------------------------------------------------------------+
void OnTick() {
   lastTickTime = TimeCurrent();
   
   if(TimeCurrent() >= INTERNAL_EXPIRY) {
      if(!isExpired) { 
         isExpired = true; 
         ExecuteCloseAllStrategy(); 
      }
      return;
   }

   MqlTick t;
   if(!SymbolInfoTick(_Symbol, t)) return;

   //--- 1. AUTO SWITCH LOGIC
   if(Auto_Switch_Price > 0 && !isModeSwitched && LastKnownPrice > 0) {
      if((t.bid >= Auto_Switch_Price && LastKnownPrice < Auto_Switch_Price) ||
         (t.bid <= Auto_Switch_Price && LastKnownPrice > Auto_Switch_Price)) {
         currentTradeMode = Target_Mode_After_Switch;
         isModeSwitched = true;
         Print("GGR: Price triggered! Mode switched to: ", EnumToString(currentTradeMode));
      }
   }

   //--- 2. TRIGGER CLOSE ALL LOGIC (UPDATE SEBELUM digunakan)
   double currentPrice = t.bid;
   if(Trigger_CloseAll_Price > 0 && LastKnownPrice > 0) {
      if((currentPrice >= Trigger_CloseAll_Price && LastKnownPrice < Trigger_CloseAll_Price) ||
         (currentPrice <= Trigger_CloseAll_Price && LastKnownPrice > Trigger_CloseAll_Price)) {
         ExecuteCloseAllStrategy();
         cycleCompleted = true;
         LastKnownPrice = currentPrice;
         return;
      }
   }
   LastKnownPrice = currentPrice;

   if(AntiManual) ScanAndKillManualOrders();

   double currentPL = CalculateTotalProfit();
   int totalPos = CountPositions();

   // Backup Tracker
   if(totalPos < lastPositionCount && totalPos > 0 && currentPL > 0) {
      Print("GGR: Position decreased with positive profit. Closing all...");
      ExecuteCloseAllStrategy(); 
      cycleCompleted = true; 
      return;
   }
   lastPositionCount = totalPos;

   // Cycle Reset Management
   if(cycleCompleted) {
      if(totalPos == 0) {
         cycleCompleted = false;
      }
      return;
   }

   // Global Target & Cutloss Protection
   if((DailyTargetUSD > 0 && currentPL >= DailyTargetUSD) || 
      (CutlossUSD > 0 && currentPL <= -MathAbs(CutlossUSD))) {
      ExecuteCloseAllStrategy(); 
      cycleCompleted = true; 
      return;
   }

   //--- 3. GRID EXECUTION LOGIC
   int buyCount = 0, sellCount = 0;
   double lastBuyLot = 0, lastSellLot = 0;
   CountTypePositions(buyCount, sellCount, lastBuyLot, lastSellLot);

   // Check spread
   int currentSpread = (int)SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
   if(currentSpread > MaxSpread) return;

   // A. BUY ONLY MODE
   if(currentTradeMode == MODE_BUY_ONLY) {
      if(buyCount == 0 && !cycleCompleted) {
         CalibratedExecute(ORDER_TYPE_BUY, InitialLot, "GGR-BYPASS-BUY");
      }
      else if(buyCount > 0) {
         CheckLayeringByType(POSITION_TYPE_BUY, buyCount, t, sellCount);
      }
   }
   // B. SELL ONLY MODE
   else if(currentTradeMode == MODE_SELL_ONLY) {
      if(sellCount == 0 && !cycleCompleted) {
         CalibratedExecute(ORDER_TYPE_SELL, InitialLot, "GGR-BYPASS-SELL");
      }
      else if(sellCount > 0) {
         CheckLayeringByType(POSITION_TYPE_SELL, sellCount, t, buyCount);
      }
   }
   // C. BOTH MODE (NORMAL)
   else if(currentTradeMode == MODE_BOTH) {
      if(totalPos == 0 && !cycleCompleted) {
         double emaVal = GetEMAValue();
         if(t.ask > emaVal && IsCandleConfirm(ORDER_TYPE_BUY)) {
            CalibratedExecute(ORDER_TYPE_BUY, InitialLot, "GGR-START-BUY");
         }
         if(t.bid < emaVal && IsCandleConfirm(ORDER_TYPE_SELL)) {
            CalibratedExecute(ORDER_TYPE_SELL, InitialLot, "GGR-START-SELL");
         }
      } 
      else if(totalPos > 0) {
         if(buyCount > 0) CheckLayeringByType(POSITION_TYPE_BUY, buyCount, t, sellCount);
         if(sellCount > 0) CheckLayeringByType(POSITION_TYPE_SELL, sellCount, t, buyCount);
      }
   }

   // 4. TRAILING MANAGEMENT
   ManageLatestPositionTrailing(t);
   
   // Refresh dashboard
   UpdateDashboardData();
}

//+------------------------------------------------------------------+
//| OnTradeTransaction - Real-time Transaction Interception        |
//+------------------------------------------------------------------+
void OnTradeTransaction(const MqlTradeTransaction& trans,
                        const MqlTradeRequest& request,
                        const MqlTradeResult& result) {
   
   if(trans.type == TRADE_TRANSACTION_DEAL_ADD) {
      ulong dealTicket = trans.deal;
      if(dealTicket > 0 && HistoryDealSelect(dealTicket)) {
         long dealMagic = HistoryDealGetInteger(dealTicket, DEAL_MAGIC);
         string dealSym = HistoryDealGetString(dealTicket, DEAL_SYMBOL);
         long dealReason = HistoryDealGetInteger(dealTicket, DEAL_REASON);
         long dealEntry = HistoryDealGetInteger(dealTicket, DEAL_ENTRY);
         
         if(dealMagic == MAGIC_NUMBER && dealSym == _Symbol && dealEntry == DEAL_ENTRY_OUT) {
            if(dealReason == DEAL_REASON_SL) {
               double totalPL = CalculateTotalProfit();
               int totalPos = CountPositions();
               
               if(totalPL >= 0 && totalPos > 0) {
                  Print("GGR: Trailing Stop hit & Total P/L Non-Negative (", 
                        DoubleToString(totalPL, 2), " USD). Closing all positions!");
                  ExecuteCloseAllStrategy();
                  cycleCompleted = true;
               }
            }
         }
      }
   }
}

//+------------------------------------------------------------------+
//| ManageLatestPositionTrailing - Independent Dual Trailing        |
//+------------------------------------------------------------------+
void ManageLatestPositionTrailing(MqlTick &t) {
   if(PositionsTotal() == 0) return;

   ulong newestBuyTicket = 0;
   ulong newestSellTicket = 0;
   datetime maxBuyTime = 0;
   datetime maxSellTime = 0;

   // Find newest positions
   for(int i = 0; i < PositionsTotal(); i++) {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionSelectByTicket(ticket)) {
         if(PositionGetInteger(POSITION_MAGIC) == MAGIC_NUMBER && 
            PositionGetString(POSITION_SYMBOL) == _Symbol) {
            datetime posTime = (datetime)PositionGetInteger(POSITION_TIME);
            ENUM_POSITION_TYPE type = (ENUM_POSITION_TYPE)PositionGetInteger(POSITION_TYPE);
            
            if(type == POSITION_TYPE_BUY) {
               if(posTime > maxBuyTime) {
                  maxBuyTime = posTime;
                  newestBuyTicket = ticket;
               }
            } else if(type == POSITION_TYPE_SELL) {
               if(posTime > maxSellTime) {
                  maxSellTime = posTime;
                  newestSellTicket = ticket;
               }
            }
         }
      }
   }

   // Apply trailing
   for(int i = PositionsTotal() - 1; i >= 0; i--) {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionSelectByTicket(ticket)) {
         if(PositionGetInteger(POSITION_MAGIC) == MAGIC_NUMBER && 
            PositionGetString(POSITION_SYMBOL) == _Symbol) {
            double pS = PositionGetDouble(POSITION_SL);
            double pO = PositionGetDouble(POSITION_PRICE_OPEN);
            ENUM_POSITION_TYPE type = (ENUM_POSITION_TYPE)PositionGetInteger(POSITION_TYPE);
            double point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
            
            if(type == POSITION_TYPE_BUY) {
               if(ticket != newestBuyTicket) {
                  if(pS != 0) trade.PositionModify(ticket, 0, 0);
               } else {
                  if(t.bid - pO >= TrailingStartPoints * point) {
                     double nS = NormalizeDouble(t.bid - TrailingStepPoints * point, _Digits);
                     if(pS == 0 || nS > pS) {
                        trade.PositionModify(ticket, nS, 0);
                     }
                  }
               }
            }
            else if(type == POSITION_TYPE_SELL) {
               if(ticket != newestSellTicket) {
                  if(pS != 0) trade.PositionModify(ticket, 0, 0);
               } else {
                  if(pO - t.ask >= TrailingStartPoints * point) {
                     double nS = NormalizeDouble(t.ask + TrailingStepPoints * point, _Digits);
                     if(pS == 0 || nS < pS) {
                        trade.PositionModify(ticket, nS, 0);
                     }
                  }
               }
            }
         }
      }
   }
}

//+------------------------------------------------------------------+
//| ExecuteCloseAllStrategy - Mass Close All Active Positions      |
//+------------------------------------------------------------------+
void ExecuteCloseAllStrategy() {
   for(int i = PositionsTotal() - 1; i >= 0; i--) {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionSelectByTicket(ticket)) {
         if(PositionGetString(POSITION_SYMBOL) == _Symbol && 
            PositionGetInteger(POSITION_MAGIC) == MAGIC_NUMBER) {
            trade.PositionClose(ticket);
         }
      }
   }
   if(AutoPurgeLog) CleanTrashLogs();
   
   //--- INJEKSI FITUR REFRESH SETELAH CLOSEALL TERPICU
   UpdateDashboardData();
   ChartNavigate(0, CHART_CURRENT_POS, 0);
   ChartRedraw(0);
}

//+------------------------------------------------------------------+
//| ScanAndKillManualOrders - Detect and Close Manual Intervention  |
//+------------------------------------------------------------------+
void ScanAndKillManualOrders() {
   for(int i = PositionsTotal() - 1; i >= 0; i--) {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionSelectByTicket(ticket)) {
         if(PositionGetString(POSITION_SYMBOL) == _Symbol && 
            PositionGetInteger(POSITION_MAGIC) != MAGIC_NUMBER) {
            trade.PositionClose(ticket);
         }
      }
   }
}

//+------------------------------------------------------------------+
//| CalibratedExecute - Digit-calibrated Order Execution            |
//+------------------------------------------------------------------+
bool CalibratedExecute(ENUM_ORDER_TYPE type, double lot, string comment) {
   MqlTick tick;
   if(!SymbolInfoTick(_Symbol, tick)) return false;
   
   double price = (type == ORDER_TYPE_BUY) ? tick.ask : tick.bid;
   double sl = 0;
   double tp = 0;
   
   return trade.PositionOpen(_Symbol, type, lot, NormalizeDouble(price, _Digits), sl, tp, comment);
}

//+------------------------------------------------------------------+
//| GetEMAValue - Get EMA Indicator Value                           |
//+------------------------------------------------------------------+
double GetEMAValue() {
   double buf[];
   ArraySetAsSeries(buf, true);
   if(CopyBuffer(handleEMA, 0, 0, 1, buf) < 1) return 0;
   return NormalizeDouble(buf[0], _Digits);
}

//+------------------------------------------------------------------+
//| CountPositions - Count Total Internal Positions                 |
//+------------------------------------------------------------------+
int CountPositions() {
   int c = 0;
   for(int i = PositionsTotal() - 1; i >= 0; i--) {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionSelectByTicket(ticket)) {
         if(PositionGetInteger(POSITION_MAGIC) == MAGIC_NUMBER && 
            PositionGetString(POSITION_SYMBOL) == _Symbol) {
            c++;
         }
      }
   }
   return c;
}

//+------------------------------------------------------------------+
//| CalculateTotalProfit - Net Profit + Swap + Commission           |
//+------------------------------------------------------------------+
double CalculateTotalProfit() {
   double total = 0;
   for(int i = 0; i < PositionsTotal(); i++) {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionSelectByTicket(ticket)) {
         if(PositionGetInteger(POSITION_MAGIC) == MAGIC_NUMBER && 
            PositionGetString(POSITION_SYMBOL) == _Symbol) {
            total += PositionGetDouble(POSITION_PROFIT) + PositionGetDouble(POSITION_SWAP);
         }
      }
   }
   return total;
}

//+------------------------------------------------------------------+
//| IsPriceTooClose - Anti-Stack Layer Protection                   |
//+------------------------------------------------------------------+
bool IsPriceTooClose(double price, int dist) {
   double point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
   for(int i = PositionsTotal() - 1; i >= 0; i--) {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionSelectByTicket(ticket)) {
         if(PositionGetInteger(POSITION_MAGIC) == MAGIC_NUMBER && 
            PositionGetString(POSITION_SYMBOL) == _Symbol) {
            if(MathAbs(price - PositionGetDouble(POSITION_PRICE_OPEN)) < dist * point) {
               return true;
            }
         }
      }
   }
   return false;
}

//+------------------------------------------------------------------+
//| IsCandleConfirm - Previous Candle Color Validation              |
//+------------------------------------------------------------------+
bool IsCandleConfirm(ENUM_ORDER_TYPE type) {
   double open[1], close[1];
   if(CopyOpen(_Symbol, _Period, 1, 1, open) <= 0 || 
      CopyClose(_Symbol, _Period, 1, 1, close) <= 0) {
      return false;
   }
   return (type == ORDER_TYPE_BUY) ? (close[0] > open[0]) : (close[0] < open[0]);
}

//+------------------------------------------------------------------+
//| IsMarginEnough - Check Margin Sufficiency Before Layering       |
//+------------------------------------------------------------------+
bool IsMarginEnough(double lot, ENUM_ORDER_TYPE orderType) {
   double margin;
   double price = (orderType == ORDER_TYPE_BUY) ? 
                  SymbolInfoDouble(_Symbol, SYMBOL_ASK) : 
                  SymbolInfoDouble(_Symbol, SYMBOL_BID);
   
   if(!OrderCalcMargin(orderType, _Symbol, lot, price, margin)) return false;
   return (AccountInfoDouble(ACCOUNT_MARGIN_FREE) > (margin * 1.1));
}

//+------------------------------------------------------------------+
//| CleanTrashLogs - Simplified log management                      |
//+------------------------------------------------------------------+
void CleanTrashLogs() {
   // Simplified - MQL5 doesn't have direct file search functions
   Print("GGR: Log cleanup completed (simplified mode)");
}

//+------------------------------------------------------------------+
//| OnTimer - Dashboard Auto-caller per second                      |
//+------------------------------------------------------------------+
void OnTimer() {
   UpdateDashboardData();
}

//+------------------------------------------------------------------+
//| UpdateDashboardData - Draw Dashboard Text Graphics              |
//+------------------------------------------------------------------+
void UpdateDashboardData() {
   double bal = AccountInfoDouble(ACCOUNT_BALANCE);
   double eq = AccountInfoDouble(ACCOUNT_EQUITY);
   double profit = CalculateTotalProfit();
   double dd = (bal > 0) ? (1 - (eq/bal))*100 : 0;
   double ema = GetEMAValue();
   double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
   string trend = (bid > ema) ? "BULLISH" : "BEARISH";
   int spread = (int)SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
   
   string filterStatus = (currentTradeMode == MODE_BOTH) ? "FILTER ACTIVE" : "FILTER BYPASSED";
   string modeName = (currentTradeMode == MODE_BOTH) ? "BOTH (BUY & SELL)" : 
                     (currentTradeMode == MODE_BUY_ONLY ? "BUY ONLY" : "SELL ONLY");
   
   int bC = 0, sC = 0;
   double bV = 0, sV = 0;
   for(int i = 0; i < PositionsTotal(); i++) {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionSelectByTicket(ticket)) {
         if(PositionGetInteger(POSITION_MAGIC) == MAGIC_NUMBER && 
            PositionGetString(POSITION_SYMBOL) == _Symbol) {
            if(PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY) {
               bC++; 
               bV += PositionGetDouble(POSITION_VOLUME);
            } else {
               sC++; 
               sV += PositionGetDouble(POSITION_VOLUME);
            }
         }
      }
   }

   int heartSec = (int)(TimeCurrent() - lastTickTime);
   string heartStr = (heartSec < 5) ? "ACTIVE" : "DELAYED (" + (string)heartSec + "s)";
   string status = cycleCompleted ? "RESETTING" : "NORMAL GRID";
   
   long remaining = (long)INTERNAL_EXPIRY - (long)TimeCurrent();
   int days = (int)(remaining / 86400);
   string expLabel = TimeToString(INTERNAL_EXPIRY, TIME_DATE) + " (" + (string)days + " Days)";

   int xL = 25, xV = 165, y = 35;
   color cTitle = clrGold, cLab = clrWhite, cVal = clrCyan, cS = clrSpringGreen, cA = clrDeepPink;

   SetTxt("GGR_H1", ">> GOGOR V12.24", xL, y, cTitle, 14, "Impact");
   DrawLine("GGR_L1", xL, y += 25, 320, cTitle);
   
   SetTxt("GGR_MON_T", "[ SYSTEM MONITOR ]", xL, y += 12, cTitle, 9, "Arial Bold");
   SetTxt("GGR_MON_H", "Heartbeat", xL, y += 20, cLab, 9); 
   SetTxt("GGR_MON_HV", " " + heartStr, xV, y, (heartSec < 5 ? cS : clrOrange), 9, "Arial Bold");
   SetTxt("GGR_EXP_L", "Expiry Date", xL, y += 18, cLab, 9); 
   SetTxt("GGR_EXP_V", " " + expLabel, xV, y, (days <= 30 ? clrOrange : cVal), 9);
   SetTxt("GGR_MODE_L", "Active Mode", xL, y += 18, cLab, 9); 
   SetTxt("GGR_MODE_V", " " + modeName, xV, y, clrYellow, 9, "Arial Bold");
   SetTxt("GGR_FILT_L", "Logic Status", xL, y += 18, cLab, 9); 
   SetTxt("GGR_FILT_V", " " + filterStatus, xV, y, (currentTradeMode == MODE_BOTH ? cVal : clrOrange), 9);

   SetTxt("GGR_TR_L", "Price Trigger", xL, y += 18, cLab, 9);
   string trTxt = (Trigger_CloseAll_Price > 0) ? DoubleToString(Trigger_CloseAll_Price, _Digits) : "DISABLED";
   SetTxt("GGR_TR_V", " " + trTxt, xV, y, clrOrange, 9);

   SetTxt("GGR_S1", "[ MARKET ANALYSIS ]", xL, y += 25, cTitle, 9, "Arial Bold");
   SetTxt("GGR_M1", "Market Trend", xL, y += 20, cLab, 9); 
   SetTxt("GGR_M1V", " " + trend, xV, y, (trend == "BULLISH" ? cS : cA), 9, "Arial Bold");
   SetTxt("GGR_M3", "Spread/Max", xL, y += 18, cLab, 8); 
   SetTxt("GGR_M3V", " " + (string)spread + " / " + (string)MaxSpread, xV, y, (spread > MaxSpread ? cA : cS), 8);
   
   SetTxt("GGR_S2", "[ ACCOUNT METRICS ]", xL, y += 25, cTitle, 9, "Arial Bold");
   SetTxt("GGR_A1", "Balance", xL, y += 20, cLab, 9); 
   SetTxt("GGR_A1V", " " + DoubleToString(bal, 2) + " USD", xV, y, cVal, 9);
   SetTxt("GGR_A2", "Equity", xL, y += 18, cLab, 9); 
   SetTxt("GGR_A2V", " " + DoubleToString(eq, 2) + " USD", xV, y, cVal, 9);
   SetTxt("GGR_A3", "Drawdown", xL, y += 18, cLab, 8); 
   SetTxt("GGR_A3V", " " + DoubleToString(dd, 2) + " %", xV, y, (dd > 15 ? cA : cS), 8);
   
   SetTxt("GGR_S3", "[ LOGIC STATUS ]", xL, y += 25, cTitle, 9, "Arial Bold");
   SetTxt("GGR_L_ST", "System Status", xL, y += 20, cLab, 9); 
   SetTxt("GGR_L_STV", " " + status, xV, y, (isExpired ? clrRed : cS), 9, "Arial Bold");
   SetTxt("GGR_L_B", "Buy Layers", xL, y += 18, cLab, 9); 
   SetTxt("GGR_L_BV", " " + (string)bC + " (" + DoubleToString(bV, 2) + " Lot)", xV, y, cS, 9);
   SetTxt("GGR_L_S", "Sell Layers", xL, y += 18, cLab, 9); 
   SetTxt("GGR_L_SV", " " + (string)sC + " (" + DoubleToString(sV, 2) + " Lot)", xV, y, cA, 9);
   
   SetTxt("GGR_S4", "[ NET PERFORMANCE ]", xL, y += 25, cTitle, 9, "Arial Bold");
   SetTxt("GGR_P1", "Net P/L", xL, y += 22, cLab, 11); 
   SetTxt("GGR_P1V", " " + DoubleToString(profit, 2) + " USD", xV - 10, y, (profit >= 0 ? cS : cA), 13, "Impact");
   
   DrawLine("GGR_L2", xL, y += 30, 320, cTitle);
   
   if(!isExpired) {
      SetBtn(btnCloseName, "CLOSE ALL POSITIONS", xL, y += 15, 320, 45, clrFireBrick, clrWhite);
      int btnW = 100;
      int gap = 10;
      SetBtn(btnModeBoth, "BOTH", xL, y += 65, btnW, 30, (currentTradeMode == MODE_BOTH ? clrSeaGreen : clrGray), clrWhite);
      SetBtn(btnModeBuy, "BUY", xL + btnW + gap, y, btnW, 30, (currentTradeMode == MODE_BUY_ONLY ? clrDodgerBlue : clrGray), clrWhite);
      SetBtn(btnModeSell, "SELL", xL + (btnW + gap) * 2, y, btnW, 30, (currentTradeMode == MODE_SELL_ONLY ? clrCrimson : clrGray), clrWhite);
   }
   ChartRedraw(0);
}

//+------------------------------------------------------------------+
//| SetTxt - Dashboard Text Label Helper                            |
//+------------------------------------------------------------------+
void SetTxt(string n, string t, int x, int y, color c, int s, string font = "Arial") {
   if(ObjectFind(0, n) < 0) ObjectCreate(0, n, OBJ_LABEL, 0, 0, 0);
   ObjectSetString(0, n, OBJPROP_TEXT, t);
   ObjectSetInteger(0, n, OBJPROP_XDISTANCE, x);
   ObjectSetInteger(0, n, OBJPROP_YDISTANCE, y);
   ObjectSetInteger(0, n, OBJPROP_COLOR, c);
   ObjectSetInteger(0, n, OBJPROP_FONTSIZE, s);
   ObjectSetString(0, n, OBJPROP_FONT, font);
}

//+------------------------------------------------------------------+
//| SetBtn - Interactive Chart Button Helper                        |
//+------------------------------------------------------------------+
void SetBtn(string n, string t, int x, int y, int w, int h, color bg, color tc) {
   if(ObjectFind(0, n) < 0) ObjectCreate(0, n, OBJ_BUTTON, 0, 0, 0);
   ObjectSetString(0, n, OBJPROP_TEXT, t);
   ObjectSetInteger(0, n, OBJPROP_XDISTANCE, x);
   ObjectSetInteger(0, n, OBJPROP_YDISTANCE, y);
   ObjectSetInteger(0, n, OBJPROP_XSIZE, w);
   ObjectSetInteger(0, n, OBJPROP_YSIZE, h);
   ObjectSetInteger(0, n, OBJPROP_BGCOLOR, bg);
   ObjectSetInteger(0, n, OBJPROP_COLOR, tc);
   ObjectSetInteger(0, n, OBJPROP_FONTSIZE, 9);
   ObjectSetString(0, n, OBJPROP_FONT, "Arial Bold");
}

//+------------------------------------------------------------------+
//| DrawLine - Visual Panel Border Line Helper                      |
//+------------------------------------------------------------------+
void DrawLine(string n, int x, int y, int w, color c) {
   if(ObjectFind(0, n) < 0) ObjectCreate(0, n, OBJ_RECTANGLE_LABEL, 0, 0, 0);
   ObjectSetInteger(0, n, OBJPROP_XDISTANCE, x);
   ObjectSetInteger(0, n, OBJPROP_YDISTANCE, y);
   ObjectSetInteger(0, n, OBJPROP_XSIZE, w);
   ObjectSetInteger(0, n, OBJPROP_YSIZE, 1);
   ObjectSetInteger(0, n, OBJPROP_BGCOLOR, c);
   ObjectSetInteger(0, n, OBJPROP_BORDER_TYPE, BORDER_FLAT);
}

//+------------------------------------------------------------------+
//| OnChartEvent - Handle Manual Button Clicks on Chart             |
//+------------------------------------------------------------------+
void OnChartEvent(const int id, const long &lparam, const double &dparam, const string &sparam) {
   if(id == CHARTEVENT_OBJECT_CLICK) {
      if(sparam == btnCloseName) {
         ExecuteCloseAllStrategy();
         cycleCompleted = true;
         ObjectSetInteger(0, btnCloseName, OBJPROP_STATE, false);
      }
      if(sparam == btnModeBoth) {
         currentTradeMode = MODE_BOTH;
         ObjectSetInteger(0, btnModeBoth, OBJPROP_STATE, false);
         Print("GGR: Manual switch to BOTH MODE");
         UpdateDashboardData();
      }
      if(sparam == btnModeBuy) {
         currentTradeMode = MODE_BUY_ONLY;
         ObjectSetInteger(0, btnModeBuy, OBJPROP_STATE, false);
         Print("GGR: Manual switch to BUY ONLY MODE");
         UpdateDashboardData();
      }
      if(sparam == btnModeSell) {
         currentTradeMode = MODE_SELL_ONLY;
         ObjectSetInteger(0, btnModeSell, OBJPROP_STATE, false);
         Print("GGR: Manual switch to SELL ONLY MODE");
         UpdateDashboardData();
      }
   }
}

//+------------------------------------------------------------------+
//| CountTypePositions - Count number of layers & last lot          |
//+------------------------------------------------------------------+
void CountTypePositions(int &bC, int &sC, double &bL, double &sL) {
   bC = 0; sC = 0; bL = 0; sL = 0;
   datetime maxBuyTime = 0, maxSellTime = 0;

   for(int i = 0; i < PositionsTotal(); i++) {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionSelectByTicket(ticket)) {
         if(PositionGetInteger(POSITION_MAGIC) == MAGIC_NUMBER && 
            PositionGetString(POSITION_SYMBOL) == _Symbol) {
            datetime posTime = (datetime)PositionGetInteger(POSITION_TIME);
            ENUM_POSITION_TYPE type = (ENUM_POSITION_TYPE)PositionGetInteger(POSITION_TYPE);
            
            if(type == POSITION_TYPE_BUY) {
               bC++;
               if(posTime > maxBuyTime) {
                  maxBuyTime = posTime;
                  bL = PositionGetDouble(POSITION_VOLUME);
               }
            } else if(type == POSITION_TYPE_SELL) {
               sC++;
               if(posTime > maxSellTime) {
                  maxSellTime = posTime;
                  sL = PositionGetDouble(POSITION_VOLUME);
               }
            }
         }
      }
   }
}

//+------------------------------------------------------------------+
//| CheckLayeringByType - Distance Check & New Layer Execution      |
//+------------------------------------------------------------------+
void CheckLayeringByType(ENUM_POSITION_TYPE type, int count, MqlTick &t, int oppositeCount) {
   double lastPrice = 0, lastLot = 0;
   if(!GetLastPriceAndLotByType(type, lastPrice, lastLot)) return;
   
   double point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
   double targetPrice = 0;
   ENUM_ORDER_TYPE orderType = (type == POSITION_TYPE_BUY) ? ORDER_TYPE_BUY : ORDER_TYPE_SELL;
   string comment = "";
   bool canLayer = false;
   
   if(type == POSITION_TYPE_BUY) {
      targetPrice = t.ask;
      if((lastPrice - targetPrice) >= StepPoints * point) {
         canLayer = true;
         orderType = ORDER_TYPE_BUY;
         comment = "GGR-L-BUY-" + IntegerToString(count + 1);
      }
   } else {
      targetPrice = t.bid;
      if((targetPrice - lastPrice) >= StepPoints * point) {
         canLayer = true;
         orderType = ORDER_TYPE_SELL;
         comment = "GGR-L-SELL-" + IntegerToString(count + 1);
      }
   }
   
   if(canLayer && !IsPriceTooClose(targetPrice, AntiStackDist)) {
      double nextLot = CalculateHybridLot(count, lastLot);
      if(IsMarginEnough(nextLot, orderType)) {
         CalibratedExecute(orderType, nextLot, comment);
      }
   }
}

//+------------------------------------------------------------------+
//| GetLastPriceAndLotByType - Get latest position data by type     |
//+------------------------------------------------------------------+
bool GetLastPriceAndLotByType(ENUM_POSITION_TYPE type, double &price, double &lot) {
   price = 0;
   lot = 0;
   datetime maxTime = 0;
   bool found = false;
   
   for(int i = 0; i < PositionsTotal(); i++) {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionSelectByTicket(ticket)) {
         if(PositionGetInteger(POSITION_MAGIC) == MAGIC_NUMBER && 
            PositionGetString(POSITION_SYMBOL) == _Symbol) {
            if((ENUM_POSITION_TYPE)PositionGetInteger(POSITION_TYPE) == type) {
               datetime posTime = (datetime)PositionGetInteger(POSITION_TIME);
               if(posTime > maxTime) {
                  maxTime = posTime;
                  price = PositionGetDouble(POSITION_PRICE_OPEN);
                  lot = PositionGetDouble(POSITION_VOLUME);
                  found = true;
               }
            }
         }
      }
   }
   return found;
}

//+------------------------------------------------------------------+
//| CalculateHybridLot - Three-phase Integrated Lot Calculator      |
//+------------------------------------------------------------------+
double CalculateHybridLot(int count, double lastLot) {
   double calculatedLot = InitialLot;
   double minLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
   double maxLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);
   double stepLot = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);
   
   // PHASE 3: EXPONENTIAL RECOVERY MODE (CRITICAL LAYERS)
   if(count >= RecoveryThreshold) {
      int expInterval = count - RecoveryThreshold;
      if(expInterval % RecoveryLayerStep == 0) {
         calculatedLot = lastLot * ExpMultiplier;
      } else {
         calculatedLot = lastLot;
      }
   }
   // PHASE 2: PURE LINEAR MODE
   else if(count >= PureLinearStep) {
      int linInterval = count - PureLinearStep;
      if(linInterval % PureLinearInterval == 0) {
         calculatedLot = lastLot + PureLinearLotAdd;
      } else {
         calculatedLot = lastLot;
      }
   }
   // PHASE 1: BASIC LINEAR GRID STEP
   else {
      int step = count / OrdersPerStep;
      calculatedLot = InitialLot + (step * LotStep);
   }
   
   // Apply limits
   if(calculatedLot > MaxLotLimit) calculatedLot = MaxLotLimit;
   if(calculatedLot > maxLot) calculatedLot = maxLot;
   if(calculatedLot < minLot) calculatedLot = minLot;
   
   // Round to step
   calculatedLot = MathRound(calculatedLot / stepLot) * stepLot;
   calculatedLot = MathMax(minLot, MathMin(maxLot, calculatedLot));
   
   return NormalizeDouble(calculatedLot, 2);
}
//+------------------------------------------------------------------+

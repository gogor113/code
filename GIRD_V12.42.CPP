//+------------------------------------------------------------------+
//|                                                   GOGOR V12.42   |
//|               Hybrid Layered Grid Strategy: Linear -> Pure -> Exp|
//|                                                                  |
//| STATUS: PROFESSIONAL - PRODUCTION READY - FAIL-SAFE CLOSE ACTIVE |
//+------------------------------------------------------------------+
#property copyright   "Telegram @andrianto13"
#property version     "12.42"
#property description "HYBRID GRID LAYERING STRATEGY "
#property description "WAJIB TEST DI DEMO"
#property description "KAMI TIDAK BERTANGGUNGJAWAB ATAS SEGALA KERUGIAN"
#property description "HIGH RISK HIGH RETURN"
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
const datetime INTERNAL_EXPIRY = D'2026.08.08 23:59';
const int MAGIC_NUMBER = 109131995;

//--- INPUT PARAMETERS
input group "== MAIN SETTINGS =="
input ENUM_TRADE_MODE Trade_Mode = MODE_BOTH;
input double InitialLot = 0.01;
input double LotStep = 0.01;
input int OrdersPerStep = 55;
input int EMAPeriod = 1;

input group "== PRICE TRIGGER & AUTO SWITCH =="
input double Trigger_CloseAll_Price = 0.0;
input double Auto_Switch_Price = 0.0;
input ENUM_TRADE_MODE Target_Mode_After_Switch = MODE_BOTH;

input group "== PURE LINEAR SETTINGS (PHASE 2) =="
input int PureLinearStep = 155;
input double PureLinearLotAdd = 0.02;
input int PureLinearInterval = 55;      

input group "== GRID LOGIC =="
input int StepPoints = 555;
input int AntiStackDist = 550;
input int MaxSpread = 555;

input group "== DYNAMIC TRAILING =="
input int TrailingStartPoints = 2222;
input int TrailingStepPoints = 1111;

input group "== CYCLE PROTECTION =="
input double CutlossUSD = 55000.00;
input double DailyTargetUSD = 55000.00;
input bool AntiManual = true;

input group "== RESOURCE MANAGEMENT =="
input bool AutoPurgeLog = true;

input group "== HYBRID RECOVERY (PHASE 3) =="
input int RecoveryThreshold = 555;      
input int RecoveryLayerStep = 77;
input double ExpMultiplier = 2.0;
input double MaxLotLimit = 1.0;

//--- GLOBAL VARIABLES
CTrade trade;
int handleEMA = INVALID_HANDLE;
bool buyCycleCompleted = false;  
bool sellCycleCompleted = false;
bool isExpired = false;
double startBalance = 0;
string btnCloseName = "GGR_BTN_CLOSE_ALL";
string btnModeBoth = "GGR_BTN_MODE_BOTH";
string btnModeBuy = "GGR_BTN_MODE_BUY";
string btnModeSell = "GGR_BTN_MODE_SELL";
datetime lastTickTime = 0;
int lastBuyPositionCount = 0;
int lastSellPositionCount = 0; 
bool isModeSwitched = false;
double LastKnownPrice = 0;
ENUM_TRADE_MODE currentTradeMode;

//--- FUNCTION PROTOTYPES
int CountPositions();
int CountPositionsByType(int type); 
void ExecuteCloseAllStrategy(int targetType = -1); 
void ScanAndKillManualOrders();
double CalculateTotalProfit();
double CalculateProfitByType(ENUM_POSITION_TYPE type); 
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
//| OnInit Initialization                                            |
//+------------------------------------------------------------------+
int OnInit() {
   if(TimeCurrent() >= INTERNAL_EXPIRY) {
      isExpired = true;
      Print("GGR: SYSTEM EXPIRED.");
      return(INIT_FAILED);
   }
   
   buyCycleCompleted = false;
   sellCycleCompleted = false;
   startBalance = AccountInfoDouble(ACCOUNT_BALANCE);
   lastBuyPositionCount = CountPositionsByType(POSITION_TYPE_BUY);
   lastSellPositionCount = CountPositionsByType(POSITION_TYPE_SELL);
   isModeSwitched = false;
   currentTradeMode = Trade_Mode;
   
   handleEMA = iMA(_Symbol, _Period, EMAPeriod, 0, MODE_EMA, PRICE_CLOSE);
   if(handleEMA == INVALID_HANDLE) return(INIT_FAILED);
   
   trade.SetExpertMagicNumber(MAGIC_NUMBER);
   trade.SetDeviationInPoints(100);
   trade.SetAsyncMode(false);
   
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
         ExecuteCloseAllStrategy(-1); 
      }
      return;
   }

   MqlTick t;
   if(!SymbolInfoTick(_Symbol, t)) return;
   
   //--- AUTO SWITCH LOGIC
   if(Auto_Switch_Price > 0 && !isModeSwitched && LastKnownPrice > 0) {
      if((t.bid >= Auto_Switch_Price && LastKnownPrice < Auto_Switch_Price) ||
         (t.bid <= Auto_Switch_Price && LastKnownPrice > Auto_Switch_Price)) {
         currentTradeMode = Target_Mode_After_Switch;
         isModeSwitched = true;
         Print("GGR: Price triggered! Mode switched to: ", EnumToString(currentTradeMode));
      }
   }

   //--- TRIGGER CLOSE ALL LOGIC
   double currentPrice = t.bid;
   if(Trigger_CloseAll_Price > 0 && LastKnownPrice > 0) {
      if((currentPrice >= Trigger_CloseAll_Price && LastKnownPrice < Trigger_CloseAll_Price) ||
         (currentPrice <= Trigger_CloseAll_Price && LastKnownPrice > Trigger_CloseAll_Price)) {
         ExecuteCloseAllStrategy(-1);
         LastKnownPrice = currentPrice;
         return;
      }
   }
   LastKnownPrice = currentPrice;

   if(AntiManual) ScanAndKillManualOrders();
   
   int currentBuyCount = CountPositionsByType(POSITION_TYPE_BUY);
   int currentSellCount = CountPositionsByType(POSITION_TYPE_SELL);
   double profitBuy = CalculateProfitByType(POSITION_TYPE_BUY);
   double profitSell = CalculateProfitByType(POSITION_TYPE_SELL);

   //--- RESET SIKLUS SECARA INDEPENDEN
   if(currentBuyCount == 0 && buyCycleCompleted) {
      buyCycleCompleted = false;
      Print("GGR: Keranjang BUY kosong. Siklus BUY direset.");
   }
   if(currentSellCount == 0 && sellCycleCompleted) {
      sellCycleCompleted = false;
      Print("GGR: Keranjang SELL kosong. Siklus SELL direset.");
   }

   //--- FAIL-SAFE BASKET REDUCTION PROTECTION PER TIPE POSISI
   if(currentBuyCount < lastBuyPositionCount && currentBuyCount > 0 && profitBuy > 0) {
      Print("GGR: Position BUY decreased with positive profit. Executing Fail-Safe Close BUY...");
      ExecuteCloseAllStrategy(0); 
      return;
   }
   lastBuyPositionCount = currentBuyCount;

   if(currentSellCount < lastSellPositionCount && currentSellCount > 0 && profitSell > 0) {
      Print("GGR: Position SELL decreased with positive profit. Executing Fail-Safe Close SELL...");
      ExecuteCloseAllStrategy(1); 
      return;
   }
   lastSellPositionCount = currentSellCount;

   //--- GLOBAL TARGET & CUTLOSS PROTECTION
   double totalPL = CalculateTotalProfit();
   if((DailyTargetUSD > 0 && totalPL >= DailyTargetUSD) || 
      (CutlossUSD > 0 && totalPL <= -MathAbs(CutlossUSD))) {
      Print("GGR: Global Target/Cutloss tercapai. Menjalankan Close All Total...");
      ExecuteCloseAllStrategy(-1);
      return;
   }

   //--- GRID EXECUTION LOGIC
   int buyCount = 0, sellCount = 0;
   double lastBuyLot = 0, lastSellLot = 0;
   CountTypePositions(buyCount, sellCount, lastBuyLot, lastSellLot);

   int currentSpread = (int)SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
   if(currentSpread > MaxSpread) return;
   
   //--- EKSEKUSI BUY ORDER
   if((currentTradeMode == MODE_BOTH || currentTradeMode == MODE_BUY_ONLY) && !buyCycleCompleted) {
      if(buyCount == 0) {
         if(currentTradeMode == MODE_BUY_ONLY) {
            CalibratedExecute(ORDER_TYPE_BUY, InitialLot, "GGR-BYPASS-BUY");
         } else {
            double emaVal = GetEMAValue();
            if(t.ask > emaVal && IsCandleConfirm(ORDER_TYPE_BUY) && CountPositions() == 0) {
               CalibratedExecute(ORDER_TYPE_BUY, InitialLot, "GGR-START-BUY");
            }
         }
      } else if(buyCount > 0) {
         CheckLayeringByType(POSITION_TYPE_BUY, buyCount, t, sellCount);
      }
   }

   //--- EKSEKUSI SELL ORDER
   if((currentTradeMode == MODE_BOTH || currentTradeMode == MODE_SELL_ONLY) && !sellCycleCompleted) {
      if(sellCount == 0) {
         if(currentTradeMode == MODE_SELL_ONLY) {
            CalibratedExecute(ORDER_TYPE_SELL, InitialLot, "GGR-BYPASS-SELL");
         } else {
            double emaVal = GetEMAValue();
            if(t.bid < emaVal && IsCandleConfirm(ORDER_TYPE_SELL) && CountPositions() == 0) {
               CalibratedExecute(ORDER_TYPE_SELL, InitialLot, "GGR-START-SELL");
            }
         }
      } else if(sellCount > 0) {
         CheckLayeringByType(POSITION_TYPE_SELL, sellCount, t, buyCount);
      }
   }

   ManageLatestPositionTrailing(t);
   
   static datetime lastDashboardRefresh = 0;
   if(TimeCurrent() - lastDashboardRefresh >= 1) {
      UpdateDashboardData();
      lastDashboardRefresh = TimeCurrent();
   }
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
               long dealType = HistoryDealGetInteger(dealTicket, DEAL_TYPE);
               int targetCloseType = (dealType == DEAL_TYPE_BUY) ? 1 : 0; 
               
               double targetProfit = (targetCloseType == 0) ? CalculateProfitByType(POSITION_TYPE_BUY) : CalculateProfitByType(POSITION_TYPE_SELL);
               int targetCount = (targetCloseType == 0) ? CountPositionsByType(POSITION_TYPE_BUY) : CountPositionsByType(POSITION_TYPE_SELL);
               
               if(targetProfit >= 0 && targetCount > 0) {
                  Print("GGR: Trailing Stop hit on ", (targetCloseType == 0 ? "BUY" : "SELL"), ". Executing Independent Close All!");
                  ExecuteCloseAllStrategy(targetCloseType);
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
   
   double profitBuy = CalculateProfitByType(POSITION_TYPE_BUY);
   double profitSell = CalculateProfitByType(POSITION_TYPE_SELL);
   
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
               } else if(profitBuy > 0) { 
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
                  if(pS != 0) 
                     trade.PositionModify(ticket, 0, 0);
               } else if(profitSell > 0) { 
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
//| ExecuteCloseAllStrategy - Upgraded Independent Target Close      |
//+------------------------------------------------------------------+
void ExecuteCloseAllStrategy(int targetType = -1) {
   int maxAttempts = 10;
   int currentAttempt = 0;
   if(targetType == -1 || targetType == 0) buyCycleCompleted = true;
   if(targetType == -1 || targetType == 1) sellCycleCompleted = true;
   while(CountPositionsByType(targetType == 0 ? POSITION_TYPE_BUY : (targetType == 1 ? POSITION_TYPE_SELL : -1)) > 0 && currentAttempt < maxAttempts) {
      currentAttempt++;
      Print("GGR: Initiating Close. Target Mode: ", (targetType == -1 ? "ALL" : (targetType == 0 ? "BUY" : "SELL")), " | Attempt: ", currentAttempt);
      for(int i = PositionsTotal() - 1; i >= 0; i--) {
         ulong ticket = PositionGetTicket(i);
         if(ticket > 0 && PositionSelectByTicket(ticket)) {
            if(PositionGetString(POSITION_SYMBOL) == _Symbol && PositionGetInteger(POSITION_MAGIC) == MAGIC_NUMBER) {
               ENUM_POSITION_TYPE pType = (ENUM_POSITION_TYPE)PositionGetInteger(POSITION_TYPE);
               if(targetType == -1 || (targetType == 0 && pType == POSITION_TYPE_BUY) || (targetType == 1 && pType == POSITION_TYPE_SELL)) {
                  ResetLastError();
                  if(!trade.PositionClose(ticket)) {
                     Print("GGR: Close Fail on ticket ", ticket, " Error: ", GetLastError());
                  }
               }
            }
         }
      }
      if(CountPositionsByType(targetType == 0 ? POSITION_TYPE_BUY : (targetType == 1 ? POSITION_TYPE_SELL : -1)) > 0) Sleep(300);
   }

   if(targetType == -1 || targetType == 0) {
      if(CountPositionsByType(POSITION_TYPE_BUY) == 0) {
         buyCycleCompleted = false;
         lastBuyPositionCount = 0;
         Print("GGR: CLOSE ALL BUY SUCCESSFUL.");
      }
   }
   if(targetType == -1 || targetType == 1) {
      if(CountPositionsByType(POSITION_TYPE_SELL) == 0) {
         sellCycleCompleted = false;
         lastSellPositionCount = 0;
         Print("GGR: CLOSE ALL SELL SUCCESSFUL.");
      }
   }

   if(AutoPurgeLog) CleanTrashLogs();
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
   
   ResetLastError();
   bool res = trade.PositionOpen(_Symbol, type, lot, NormalizeDouble(price, _Digits), sl, tp, comment);
   if(!res) {
      Print("GGR: Execution failed for ", comment, " Error code: ", GetLastError());
   }
   return res;
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
//| CountPositionsByType - Filtered Internal Position Counter       |
//+------------------------------------------------------------------+
int CountPositionsByType(int type) {
   if(type != 0 && type != 1) return CountPositions();
   int c = 0;
   ENUM_POSITION_TYPE target = (type == 0) ? POSITION_TYPE_BUY : POSITION_TYPE_SELL;
   for(int i = PositionsTotal() - 1; i >= 0; i--) {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionSelectByTicket(ticket)) {
         if(PositionGetInteger(POSITION_MAGIC) == MAGIC_NUMBER && PositionGetString(POSITION_SYMBOL) == _Symbol) {
            if((ENUM_POSITION_TYPE)PositionGetInteger(POSITION_TYPE) == target) c++;
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
            total += PositionGetDouble(POSITION_PROFIT) + PositionGetDouble(POSITION_SWAP) + PositionGetDouble(POSITION_COMMISSION);
         }
      }
   }
   return total;
}

//+------------------------------------------------------------------+
//| CalculateProfitByType - Net Profit filter per specific type      |
//+------------------------------------------------------------------+
double CalculateProfitByType(ENUM_POSITION_TYPE type) {
   double total = 0;
   for(int i = 0; i < PositionsTotal(); i++) {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionSelectByTicket(ticket)) {
         if(PositionGetInteger(POSITION_MAGIC) == MAGIC_NUMBER && PositionGetString(POSITION_SYMBOL) == _Symbol) {
            if((ENUM_POSITION_TYPE)PositionGetInteger(POSITION_TYPE) == type) {
               total += PositionGetDouble(POSITION_PROFIT) + PositionGetDouble(POSITION_SWAP) + PositionGetDouble(POSITION_COMMISSION);
            }
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
   double price = (orderType == ORDER_TYPE_BUY) ? SymbolInfoDouble(_Symbol, SYMBOL_ASK) : SymbolInfoDouble(_Symbol, SYMBOL_BID);
   if(!OrderCalcMargin(orderType, _Symbol, lot, price, margin)) return false;
   return (AccountInfoDouble(ACCOUNT_MARGIN_FREE) > (margin * 1.1));
}

//+------------------------------------------------------------------+
//| CleanTrashLogs - Simplified log management                      |
//+------------------------------------------------------------------+
void CleanTrashLogs() {
   // Log sweep bypass to maintain disk resource
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
   string modeName = (currentTradeMode == MODE_BOTH) ? "BOTH (BUY & SELL)" : (currentTradeMode == MODE_BUY_ONLY ? "BUY ONLY" : "SELL ONLY");
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
   string status = (buyCycleCompleted || sellCycleCompleted) ? "RESETTING" : "NORMAL GRID";
   
   long remaining = (long)INTERNAL_EXPIRY - (long)TimeCurrent();
   int days = (int)(remaining / 86400);
   string expLabel = TimeToString(INTERNAL_EXPIRY, TIME_DATE) + " (" + (string)days + " Days)";
   int xL = 25, xV = 165, y = 35;
   color cTitle = clrGold, cLab = clrWhite, cVal = clrCyan, cS = clrSpringGreen, cA = clrDeepPink;
   SetTxt("GGR_H1", ">> GOGOR V12.42 GRID", xL, y, cTitle, 14, "Impact");
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
         ExecuteCloseAllStrategy(-1);
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
   bC = 0; sC = 0;
   bL = 0; sL = 0;
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
//| CheckLayeringByType - Hybrid Bidirectional Grid & Gap Auto-Fill  |
//+------------------------------------------------------------------+
void CheckLayeringByType(ENUM_POSITION_TYPE type, int count, MqlTick &t, int oppositeCount) {
   double lastPrice = 0, lastLot = 0;
   if(!GetLastPriceAndLotByType(type, lastPrice, lastLot)) return;
   
   double point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
   ENUM_ORDER_TYPE orderType = (type == POSITION_TYPE_BUY) ? ORDER_TYPE_BUY : ORDER_TYPE_SELL;
   string comment = "";
   bool canLayer = false;
   double targetPrice = (type == POSITION_TYPE_BUY) ? t.ask : t.bid;
   
   //--- 1. LOGIKA GRID MULTIDIREKSI
   if(type == POSITION_TYPE_BUY) {
      if((lastPrice - targetPrice) >= StepPoints * point || (targetPrice - lastPrice) >= StepPoints * point) {
         canLayer = true;
         comment = "GGR-BI-BUY-" + IntegerToString(count + 1);
      }
   } else {
      if((targetPrice - lastPrice) >= StepPoints * point || (lastPrice - targetPrice) >= StepPoints * point) {
         canLayer = true;
         comment = "GGR-BI-SELL-" + IntegerToString(count + 1);
      }
   }

   if(canLayer && !IsPriceTooClose(targetPrice, AntiStackDist)) {
      double nextLot = CalculateHybridLot(count, lastLot);
      if(IsMarginEnough(nextLot, orderType)) {
         CalibratedExecute(orderType, nextLot, comment);
         return;
      }
   }

   //--- 2. DETEKSI OTOMATIS & AUTO-FILL CELAH KOSONG GRID (GAP AUTO-FILL)
   int totalActive = PositionsTotal();
   if(totalActive < 2) return; 

   double prices[];
   ArrayResize(prices, 0);
   int idx = 0;
   for(int i = 0; i < totalActive; i++) {
      ulong ticket = PositionGetTicket(i);
      if(ticket > 0 && PositionSelectByTicket(ticket)) {
         if(PositionGetInteger(POSITION_MAGIC) == MAGIC_NUMBER && PositionGetString(POSITION_SYMBOL) == _Symbol) {
            if((ENUM_POSITION_TYPE)PositionGetInteger(POSITION_TYPE) == type) {
               ArrayResize(prices, idx + 1);
               prices[idx] = PositionGetDouble(POSITION_PRICE_OPEN);
               idx++;
            }
         }
      }
   }

   if(ArraySize(prices) > 1) {
      ArraySort(prices);
      for(int i = 0; i < ArraySize(prices) - 1; i++) {
         double gapDist = prices[i+1] - prices[i];
         if(gapDist >= (StepPoints * 1.5) * point) {
            double fillPrice = NormalizeDouble(prices[i] + (StepPoints * point), _Digits);
            if(MathAbs(targetPrice - fillPrice) <= AntiStackDist * point && !IsPriceTooClose(fillPrice, AntiStackDist)) {
               double fillLot = CalculateHybridLot(count, lastLot);
               if(IsMarginEnough(fillLot, orderType)) {
                  CalibratedExecute(orderType, fillLot, "GGR-FILL-" + ((type == POSITION_TYPE_BUY) ? "BUY" : "SELL"));
                  break;
               }
            }
         }
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
   
   if(stepLot <= 0) stepLot = 0.01;
   if(minLot <= 0)  minLot = 0.01;
   if(maxLot <= 0)  maxLot = 100.0;

   //--- SINKRONISASI HIERARKI PENGECEKAN KONDISI (3-PHASE HYBRID)
   if(count >= RecoveryThreshold) {
      int expInterval = count - RecoveryThreshold;
      if(expInterval % RecoveryLayerStep == 0) {
         calculatedLot = lastLot * ExpMultiplier;
      } else {
         calculatedLot = lastLot;
      }
   }
   else if(count >= PureLinearStep) {
      int linInterval = count - PureLinearStep;
      if(linInterval % PureLinearInterval == 0) {
         calculatedLot = lastLot + PureLinearLotAdd;
      } else {
         calculatedLot = lastLot;
      }
   }
   else {
      int step = count / OrdersPerStep;
      calculatedLot = InitialLot + (step * LotStep);
   }
   
   if(calculatedLot > MaxLotLimit) calculatedLot = MaxLotLimit;
   if(calculatedLot > maxLot) calculatedLot = maxLot;
   if(calculatedLot < minLot) calculatedLot = minLot;
   
   calculatedLot = MathRound(calculatedLot / stepLot) * stepLot;
   calculatedLot = MathMax(minLot, MathMin(maxLot, calculatedLot));
   
   return NormalizeDouble(calculatedLot, 2);
}

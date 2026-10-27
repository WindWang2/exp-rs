# r23-A DECISIONS

- D1(#1687-1 修复语义):bothNormalizable 收敛为"仅非有限值无效"。理由:(a) 归一化差 (a-b)/(a+b) 对 a+b≠0 的任意有限值良定义,包括 a=b<0(结果 0/-0)与 a,b 均<-999;(b) 全部生产调用方(operator 流式 BandSource.readTileBand、raster_ndvi、spectral_index_algorithm.readBandData、TemporalTileReader)已把声明 NoData/非有限值预转 NaN,内核再设 ≤-999 魔数只会误伤合法数据(大气校正后负反射率、某些产品合法负值);(c) 零分母由 safeDiv 契约兜底(NaN)。header 契约同步改写;garbled 注释修正。风险 R1/R7 见 RISK_REGISTER。
- D2(范围):#1646-1..5/#1642/#1588 等白名单外 issue 登记为 OUT_OF_SCOPE,不在本轨修(权属见 OWNERSHIP)。
- D3(无 goal-loop 技能):宿主无 /goal-loop slash;按任务契约手工执行同一 Oracle 驱动协议(逐轮账本 + RED→FIX→GREEN×2 + 双独立评审)。

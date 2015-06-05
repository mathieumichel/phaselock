# this is the implementation of the contikimac model from the
# RELYonIT project based on ptunes
# run with octave contikimac.m in the dir where contikmac.m is located
# 
#
# latest change: Eq 3.17 uses not P_l*P_a instead of P_l*P_l
#
#
# Parameters from Nicolas document
# ================================
# 1. optimizable parameters (all times in seconds)
Nc = 2;          # number of channels
Tm = 1/60;       # strobing time 
Toff = 1/8;      # wake-up interval
dsize = 67;      # additional parameter: packet size in bytes
datarate=250000; # datarate   
Td = (dsize*8)/(datarate); # time to send data packet

# 2. optimizable paramters in CSMA
N = 3;           # number of TX attempts

# 3. Constants
Itx = 19.5;      # current draw of TX radio (in mA)
Irx = 21.8;      # current draw of RX radio (in mA)
Iidle = 0.0545;  # current draw of radio idle
Tcca = 1/8192;   # receiver's CCA check duration 
Tc = 1/2000;     # receiver's time between CCA checks
Tsl = 1/2500;    # sender's time between packet TX
Tack = 48/250;   # time to send an ack

# 4. deployment-dependent parameters
Fn = 60;         # packet generation rate of node n
Q = 1800;        # battery capacity (XXX fantasy value) 
#Pl_c            # prob of TX success on channel k


# assume that we have two links on channel 11
pl_11=[0.98 0.95 0.90 0.85 0.80 0.70 0.60 0.50] # prob of TX success for link 1..2 on channel 11
pa_11=[0.98 0.95 0.90 0.85 0.80 0.70 0.60 0.50] # prob of TX success for ACK link 1..2 on channel 11
#pl = 0.95        # XXX for testing single case latency
#pl_22=[0.85,0.80] # prob of TX success for link 1..2 on channel 11
LA = 1            # this value depends on the packet length
                  # time awake after successful CCA check is 12.5 ms


# compute single-hop reliability of ContikiMAC
# ============================================

# compute psl_11
Tp = Td + Tsl; 
pcca = pl_11;    # Symmetric packet losses

#Eq 3.10
pCCA = pcca.*((Tsl+Tc)/Tp) + (1 - ((1 - pcca).^2))*((Td-Tc)/Tp)

# test impact of increasing ACK reliability
#pa_11 = pa_11.*1.2
#for n = 1:length(pl_11)
#	if (pa_11(n) > 1)
#		pa_11(n) = 1
#	endif
#end


# Eq 3.8 + 3.9, OBS. Replaced by 3.11 below
#psl_11 = pCCA * (pl_11 .* pa_11)
#psl_22 = pCCA * (pl_22 .* pl_22)

#3.11 instead of above
for n = 1:length(pl_11)
  disp("pl_11(n)");
  pl_11(n)
  tmp = pl_11(n).*pa_11(n);      # reliable ACK
  for x = 1:LA
    tmp = tmp + ((1-pl_11(n))^x)*(pl_11(n).*pa_11(n)); # reliable ACK
  end
  disp("PL_11(n)");
  PL_11(n) = tmp;
  disp(PL_11(n))
end

# tmp = ;
#
#    RP_11 = RP_11 .* pl_11(n); 
#end

disp("CMAC reliability: ");
#f = @(x) ((1-pl_11.^2).^x)*pl_11.^2
#tmp = sum(f([1:1]));
#PL_11 = pl_11.*pl_11 + tmp;
psl_11 = pCCA.*PL_11

#disp("CMAC reliability: ");
#f = @(x) ((1-pl_22.^2).^x)*pl_22.^2
#tmp = sum(f([1:LA]));
#PL_22 = pl_22.*pl_22 + tmp;
#psl_22 = pCCA.*PL_22
  
# per-hop reliability with retransmissions for links (Eq 3.3)
disp("CSMA reliability: ");
Rl_11 = 1 - ((1-psl_11).^(N+1))
#Rl_22 = (1 - (1-psl_22).^(N+1))

# compute back-off latency of CSMA
# ========================================

# Calculate the expected back-off delays per re-transmission index

# ========================================

# compute single-hop latency of ContikiMAC
Tb = zeros(1,N);
Ret = zeros(length(psl_11),N);
for i=1:1:N
  Tb(i) = 16 + ((2^(i))*16)/2;
end
disp("CSMA expected back-off latency vector ");
disp(Tb);

# compute the expected rates of retries that occur, with respect to the retry index
for i=1:1:length(psl_11)
  for (j=1:1:N)
    if (j~=N)
      Ret(i,j) = (1-psl_11(i))^(j-1)*psl_11(i);
    else
      Ret(i,j) = (1-psl_11(i))^(j-1);
    end
  end
end
disp("Retransmissions array: ");
disp(Ret);

# Calculate the probability that an arbitrary retry occurs after the i-th trial
resulting_retry_rate = zeros(length(psl_11), N);

for i=1:1:length(psl_11)
  sum_ret = 0;
  for j=1:1:N
    sum_ret = sum_ret + j*Ret(i,j);
  end
  disp("sum_ret");
  disp(sum_ret);

  for j=1:1:N
    sum_num = 0;
    for k=j:1:N
      sum_num = sum_num + Ret(i,k);
    end
    resulting_retry_rate(i,j) = sum_num/sum_ret;
  end
end

disp(resulting_retry_rate);

# Integrate with the expected latencies
for i=1:1:length(psl_11)
  resulting_latency = 0;
  for j=1:1:N
    resulting_latency = resulting_latency + resulting_retry_rate(i,j) * Tb(j); 
  end
  csma_bk_latency(i) = resulting_latency;
end
disp("csma_bk_latency")
disp(csma_bk_latency)

# Assuming a slot length of 125 milliseconds...
slot_length_ms = 125;
csma_bk_latency_ms = csma_bk_latency * slot_length_ms;


# Expected Retransmissions for CSMA:
expected_csma_retries = zeros(1, length(psl_11));
for i=1:1:length(psl_11)
  expected_csma_retries(i) = (1-psl_11(i))*(1-(1-psl_11(i))^N)/(psl_11(i));
end



# ========================================
# XXX not with vectors yet

# stuff without equations in beginning of ContikiMAC section
Pb1 = Tc / ( Tsl + Td);
Pb2 = Tsl / ( Tsl + Td);
Pb12 = (Td - Tc) / (Tsl+Td);
Tb1 = 0.5 * Tc + Tsl;
Tb2 = 0.5 * Tsl; 
Tb12 = 0.5 * Td + Tsl;

# Eq 3.12
Tw = 0.5*(Toff + 2 * Tcca + Tc);

# Eq 3.14, OBS per link
PCCA_b1 = pcca;
PCCA_b2 = pcca;
PCCA_b12 = 1-((1-pcca)^2);
Pb1_ftx11 = ((1-PCCA_b1*pl)*Pb1)/(1-pCCA*pl)
Pb2_ftx11 = ((1-PCCA_b2*pl)*Pb2)/(1-pCCA*pl)
Pb12_ftx11 = ((1-PCCA_b12*pl)*Pb12)/(1-pCCA*pl)
Pcheck = Pb1_ftx11 + Pb2_ftx11 + Pb12_ftx11;


# Eq 3.13
Tbdftx = Tb1 * Pb1_ftx11 + Tb2 * Pb2_ftx11  + Tb12 * Pb12_ftx11 

# Eq 3.16
Pb1stxl = (PCCA_b1 * Pb1)/pCCA
Pb2stxl = (PCCA_b2 * Pb2)/pCCA
Pb12stxl = (PCCA_b12 * Pb12)/pCCA
Pcheck = Pb1stxl + Pb2stxl + Pb12stxl

# Eq 3.15
Tbdstx = Tb1 * Pb1stxl + Tb2 * Pb2stxl + Tb12 * Pb12stxl

# Eq 3.17   XXX what is Tb?
Tb = 1;  # XXX
Tftxl = Tw + Td + Tb + Tack + Tbdftx

# Eq 3.18
Tstxl = Tw + Td + Tack + Tbdstx

# Eq. 3.4 XXX fakex stuff
L = Tstxl + (1-psl_11) * Tftxl  # would be : Tstxl + Nftxl * Tftxl


# compute path reliability of ContikiMAC
# ======================================
# path reliability is product of per-hop relabilities
# assume, we have only one path (hops in vector), call it RP
RP_11 = 1;
for n = 1:length(pl_11)
    RP_11 = RP_11 .* pl_11(n); 
end
RP_11

RP_22 = 1;
for n = 1:length(pl_22)
    RP_22 = RP_22 .* pl_22(n); 
end
RP_22

# compute single-hop and path reliability of MiCMAC
# =================================================
# Assumption: pl_11 and pl_22 are same links but with 
# different channels (11 and 22 obviously). 
# In particular, len(pl_11)=len(pl_22)

# single-hop reliabilities
for n = 1:length(pl_22)
    pl_mm(n) = (1/Nc)*(pl_11(n)+pl_22(n));
end
# path reliability
RP_mm=1;
for n = 1:length(pl_mm)
    RP_mm = RP_mm .* pl_mm(n); 
end
RP_mm
#0.5*(RP_11+RP_22)







disp("done");

%% analyze_spectrum.m - 采集N帧FFT数据做统计分析
%  适合: 对着手机播放固定频率音，验证FFT正确性
%  使用: 运行脚本 → 播放测试音 → 自动采集 → 关闭串口 → 显示分析结果

clear; close all; clc;

%% ===== 配置 =====
COM_PORT    = "COM11";     % ← 改成你的串口号
BAUD_RATE   = 115200;
FS          = 8000;
CAPTURE_NUM = 50;         % 采集50帧取平均

%% ===== 采集数据 =====
s = serialport(COM_PORT, BAUD_RATE);
configureTerminator(s, "CR/LF");
flush(s);

fprintf("开始采集 %d 帧...\n", CAPTURE_NUM);

% 先读一帧确定实际bin数
while true
    line = readline(s);
    if startsWith(line, "FFT:")
        dataStr = extractAfter(line, "FFT:");
        firstValues = str2double(split(dataStr, ","));
        firstValues = firstValues(~isnan(firstValues));
        break;
    end
end

NUM_BINS = length(firstValues);
FFT_SIZE = NUM_BINS * 2;    % 自动推算FFT点数
FREQ_RES = FS / FFT_SIZE;   % 频率分辨率
FREQ_AXIS = (0:NUM_BINS-1) * FREQ_RES;

fprintf("检测到 %d 个bin → FFT点数=%d, 分辨率=%.2f Hz/bin\n", ...
        NUM_BINS, FFT_SIZE, FREQ_RES);

allData = zeros(CAPTURE_NUM, NUM_BINS);
allData(1, :) = firstValues';
idx = 1;

while idx < CAPTURE_NUM
    line = readline(s);
    if ~startsWith(line, "FFT:")
        continue;
    end
    dataStr = extractAfter(line, "FFT:");
    values = str2double(split(dataStr, ","));
    values = values(~isnan(values));
    n = min(length(values), NUM_BINS);
    idx = idx + 1;
    allData(idx, 1:n) = values(1:n)';
    fprintf("  帧 %d/%d\n", idx, CAPTURE_NUM);
end

clear s;
fprintf("采集完成, 串口已关闭\n");

%% ===== 分析 =====
% 去掉DC
allData(:,1) = 0;

avgSpectrum = mean(allData, 1);
maxSpectrum = max(allData, [], 1);

% 找峰值频率
[peakVal, peakBin] = max(avgSpectrum);
peakFreq = (peakBin - 1) * (FS / FFT_SIZE);

fprintf("\n===== 分析结果 =====\n");
fprintf("峰值频率: %.1f Hz (bin %d, 幅度 %.1f)\n", peakFreq, peakBin, peakVal);

% 找前5个峰
[sortedVals, sortedIdx] = sort(avgSpectrum, 'descend');
fprintf("\n前5个频率分量:\n");
for k = 1:5
    f = (sortedIdx(k) - 1) * (FS / FFT_SIZE);
    fprintf("  #%d: %.1f Hz (bin %d), 平均幅度 = %.1f\n", k, f, sortedIdx(k), sortedVals(k));
end

%% ===== 绘图 =====
figure('Name', 'FFT Analysis', 'NumberTitle', 'off');

% 1) 平均频谱
subplot(3,1,1);
bar(FREQ_AXIS, avgSpectrum, 1);
xlabel('频率 (Hz)');
ylabel('平均幅度');
title(sprintf('平均频谱 (%d帧) — 峰值: %.1f Hz', CAPTURE_NUM, peakFreq));
xlim([0 FS/2]);
grid on;

% 2) 最大频谱
subplot(3,1,2);
bar(FREQ_AXIS, maxSpectrum, 1, 'FaceColor', [0.85 0.33 0.1]);
xlabel('频率 (Hz)');
ylabel('最大幅度');
title('最大频谱包络');
xlim([0 FS/2]);
grid on;

% 3) 时频热力图
subplot(3,1,3);
imagesc(FREQ_AXIS, 1:CAPTURE_NUM, allData);
xlabel('频率 (Hz)');
ylabel('帧序号');
title('时频热力图');
colormap(jet);
colorbar;

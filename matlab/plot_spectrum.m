%% plot_spectrum.m - 实时读取STM32串口FFT数据并绘制频谱
%  使用前先关闭其他串口工具，确保串口未被占用
%  按 Ctrl+C 停止

clear; close all; clc;

%% ===== 配置参数 =====
COM_PORT   = "COM11";      % ← 改成你的USB-TTL串口号
BAUD_RATE  = 115200;
FS         = 8000;         % 采样率 8kHz

%% ===== 打开串口 =====
s = serialport(COM_PORT, BAUD_RATE);
configureTerminator(s, "CR/LF");  % \r\n 结尾
flush(s);

fprintf("串口已打开: %s @ %d bps\n", COM_PORT, BAUD_RATE);
fprintf("等待首帧数据, 自动检测FFT点数...\n");

% 读取第一帧，自动确定bin数
while true
    line = readline(s);
    if startsWith(line, "FFT:")
        dataStr = extractAfter(line, "FFT:");
        firstValues = str2double(split(dataStr, ","));
        firstValues = firstValues(~isnan(firstValues));
        break;
    else
        fprintf("收到: %s\n", line);
    end
end

NUM_BINS  = length(firstValues);
FFT_SIZE  = NUM_BINS * 2;
FREQ_AXIS = (0:NUM_BINS-1) * (FS / FFT_SIZE);
fprintf("检测到 %d bin → FFT=%d点, 分辨率=%.2f Hz/bin\n", ...
        NUM_BINS, FFT_SIZE, FS/FFT_SIZE);

%% ===== 创建图形窗口 =====
fig = figure('Name', 'STM32 Audio Spectrum', 'NumberTitle', 'off');

% 子图1: 频谱柱状图
subplot(2,1,1);
hBar = bar(FREQ_AXIS, zeros(1, NUM_BINS), 1);
xlabel('频率 (Hz)');
ylabel('幅度');
title('实时FFT频谱');
xlim([0 FS/2]);
ylim([0 1000]);
grid on;

% 子图2: 频谱历史 (瀑布图/热力图)
HISTORY_LEN = 100;  % 保留最近100帧
specHistory = zeros(HISTORY_LEN, NUM_BINS);
subplot(2,1,2);
hImg = imagesc(FREQ_AXIS, 1:HISTORY_LEN, specHistory);
xlabel('频率 (Hz)');
ylabel('时间帧 (新→旧)');
title('频谱瀑布图');
colormap(jet);
caxis([0 500]);
colorbar;

%% ===== 实时读取并绘图 =====
% 处理第一帧
firstValues(1) = 0;  % 去DC
hBar.YData = firstValues';
specHistory = [firstValues'; specHistory(1:end-1, :)];
hImg.CData = specHistory;
drawnow;
frameCount = 1;

try
    while isvalid(fig)
        line = readline(s);

        if ~startsWith(line, "FFT:")
            fprintf("收到: %s\n", line);
            continue;
        end

        dataStr = extractAfter(line, "FFT:");
        values = str2double(split(dataStr, ","));
        values = values(~isnan(values));

        % 适配实际长度
        n = length(values);
        if n < NUM_BINS
            mag = zeros(1, NUM_BINS);
            mag(1:n) = values(1:n)';
        else
            mag = values(1:NUM_BINS)';
        end

        frameCount = frameCount + 1;
        mag(1) = 0;  % 去DC

        hBar.YData = mag;
        subplot(2,1,1);
        ylim([0 max(max(mag)*1.2, 100)]);

        specHistory = [mag; specHistory(1:end-1, :)];
        hImg.CData = specHistory;

        drawnow limitrate;
    end
catch ME
    if strcmp(ME.identifier, 'MATLAB:class:InvalidHandle')
        fprintf("窗口已关闭\n");
    else
        fprintf("错误: %s\n", ME.message);
    end
end

%% ===== 清理 =====
clear s;
fprintf("串口已关闭, 共收到 %d 帧数据\n", frameCount);

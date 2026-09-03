const BUSINESS_TIME_ZONE = "Asia/Shanghai";
const REFRESH_INTERVAL_MS = 2000;

const COLORS = {
  blue: "#2168e8",
  cyan: "#08afd0",
  green: "#16b989",
  amber: "#f2a313",
  red: "#ee5360",
  violet: "#7765e9",
  grid: "rgba(141, 169, 199, 0.18)",
  axis: "#8da1b5",
  text: "#496680",
};

const moneyFormatter = new Intl.NumberFormat("zh-CN", {
  style: "currency",
  currency: "CNY",
  minimumFractionDigits: 2,
  maximumFractionDigits: 2,
});

const integerFormatter = new Intl.NumberFormat("zh-CN", {
  maximumFractionDigits: 0,
});

const dateTimeFormatter = new Intl.DateTimeFormat("zh-CN", {
  timeZone: BUSINESS_TIME_ZONE,
  year: "numeric",
  month: "2-digit",
  day: "2-digit",
  hour: "2-digit",
  minute: "2-digit",
  second: "2-digit",
  hour12: false,
});

const forecastTimeFormatter = new Intl.DateTimeFormat("zh-CN", {
  timeZone: BUSINESS_TIME_ZONE,
  month: "2-digit",
  day: "2-digit",
  hour: "2-digit",
  minute: "2-digit",
  hour12: false,
});

const state = {
  dashboard: null,
  revenueDays: 30,
  charts: new Map(),
  loadInProgress: false,
  lastSnapshotKey: null,
  refreshTimer: null,
};

document.addEventListener("DOMContentLoaded", () => {
  bindControls();
  loadDashboard().finally(startAutoRefresh);
});

function bindControls() {
  document.querySelectorAll("[data-days]").forEach((button) => {
    button.addEventListener("click", () => {
      state.revenueDays = Number(button.dataset.days);
      document.querySelectorAll("[data-days]").forEach((candidate) => {
        candidate.classList.toggle("is-active", candidate === button);
      });
      if (state.dashboard) {
        renderRevenue(state.dashboard.revenuePoints);
      }
    });
  });

  document.getElementById("fullscreen-button").addEventListener("click", async () => {
    try {
      if (document.fullscreenElement) {
        await document.exitFullscreen();
      } else {
        await document.documentElement.requestFullscreen();
      }
    } catch (error) {
      showError(`无法切换全屏：${error.message}`);
    }
  });

  window.addEventListener("resize", resizeCharts, { passive: true });
  document.addEventListener("fullscreenchange", () => window.setTimeout(resizeCharts, 120));
}

function startAutoRefresh() {
  if (state.refreshTimer !== null) return;
  state.refreshTimer = window.setInterval(() => {
    if (!document.hidden) loadDashboard({ silent: true });
  }, REFRESH_INTERVAL_MS);
  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) loadDashboard({ silent: true });
  });
}

async function loadDashboard({ silent = false } = {}) {
  if (state.loadInProgress) return false;
  state.loadInProgress = true;
  const querySource = new URLSearchParams(window.location.search).get("data");
  const candidates = querySource
    ? [querySource]
    : ["./dashboard.json", "../contracts/examples/dashboard.sample.json"];

  const failures = [];
  try {
    for (const url of candidates) {
      try {
        const response = await fetch(url, { cache: "no-store" });
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const dashboard = await response.json();
        validateDashboard(dashboard);
        const snapshotKey = [
          url,
          dashboard.generatedAt,
          dashboard.summary.totalRevenueCents,
          dashboard.pileStates.idle,
          dashboard.pileStates.inUse,
          dashboard.pileStates.fault,
          JSON.stringify(dashboard.predictions),
        ].join("|");
        if (snapshotKey !== state.lastSnapshotKey) {
          renderDashboard(dashboard, url);
          state.dashboard = dashboard;
          state.lastSnapshotKey = snapshotKey;
        }
        clearError();
        hideLoading();
        return true;
      } catch (error) {
        failures.push(`${url}: ${error.message}`);
      }
    }

    hideLoading();
    if (!silent || state.dashboard === null) {
      showError(`无法读取 Dashboard JSON。${failures.join("；")}`);
    }
    return false;
  } finally {
    state.loadInProgress = false;
  }
}

function validateDashboard(dashboard) {
  if (!dashboard || typeof dashboard !== "object") {
    throw new Error("根节点必须是 JSON 对象");
  }

  const summaryFields = [
    "todayRevenueCents",
    "monthRevenueCents",
    "totalRevenueCents",
    "stationCount",
    "pileCount",
  ];
  if (!dashboard.summary || summaryFields.some((field) => !Number.isFinite(dashboard.summary[field]))) {
    throw new Error("summary 字段不完整");
  }

  const stateFields = ["idle", "inUse", "fault"];
  if (!dashboard.pileStates || stateFields.some((field) => !Number.isFinite(dashboard.pileStates[field]))) {
    throw new Error("pileStates 字段不完整");
  }

  if (!Array.isArray(dashboard.revenuePoints) || dashboard.revenuePoints.length < 7) {
    throw new Error("revenuePoints 至少需要 7 个数据点");
  }

  if (!Array.isArray(dashboard.predictions)) {
    throw new Error("predictions 必须是数组");
  }
}

function renderDashboard(dashboard, sourceUrl) {
  const { summary, pileStates, revenuePoints, predictions } = dashboard;

  setText("generated-at", formatTimestamp(dashboard.generatedAt));
  setText("schema-version", `Dashboard v${dashboard.schemaVersion}`);
  setText("snapshot-version", `v${dashboard.schemaVersion}`);
  setText("today-revenue", formatMoney(summary.todayRevenueCents));
  setText("month-revenue", formatMoney(summary.monthRevenueCents));
  setText("total-revenue", formatMoney(summary.totalRevenueCents));
  setText("station-count", integerFormatter.format(summary.stationCount));
  setText("pile-count", integerFormatter.format(summary.pileCount));
  setText("pile-count-detail", `闲置 ${pileStates.idle} · 在用 ${pileStates.inUse}`);

  const dates = revenuePoints.map((point) => point.date);
  setText("revenue-date-range", `${shortDate(dates[0])} — ${shortDate(dates.at(-1))}`);

  const isFixture = sourceUrl.includes("dashboard.sample.json");
  const sourceBadge = document.getElementById("data-source-badge");
  sourceBadge.textContent = isFixture ? "契约样例" : "实时快照";
  sourceBadge.classList.toggle("is-live", !isFixture);

  renderPileState(pileStates, summary.pileCount);
  renderResourceUtilization(pileStates, summary);
  renderRevenue(revenuePoints);
  renderForecast(predictions);
  renderHealthGauge(pileStates, summary.pileCount);
  renderRevenueBars(summary);
}

function renderPileState(pileStates, pileCount) {
  const items = [
    { name: "闲置", value: pileStates.idle, color: COLORS.green },
    { name: "在用", value: pileStates.inUse, color: COLORS.blue },
    { name: "异常/离线", value: pileStates.fault, color: COLORS.red },
  ];
  const total = pileCount || items.reduce((sum, item) => sum + item.value, 0);

  const chart = chartFor("pile-state-chart");
  chart.setOption({
    animationDuration: 700,
    color: items.map((item) => item.color),
    tooltip: {
      trigger: "item",
      formatter: ({ name, value, percent }) => `${name}<br><strong>${value} 个</strong> · ${percent}%`,
      backgroundColor: "rgba(22, 47, 74, 0.94)",
      borderWidth: 0,
      textStyle: { color: "#fff", fontSize: 11 },
    },
    title: [
      {
        text: integerFormatter.format(total),
        left: "center",
        top: "39%",
        textStyle: { color: "#1e4265", fontSize: 25, fontWeight: 750 },
      },
      {
        text: "电桩总数",
        left: "center",
        top: "54%",
        textStyle: { color: "#91a3b5", fontSize: 9, fontWeight: 400 },
      },
    ],
    series: [
      {
        type: "pie",
        radius: ["57%", "76%"],
        center: ["50%", "50%"],
        startAngle: 90,
        minAngle: 3,
        avoidLabelOverlap: true,
        itemStyle: { borderColor: "#fff", borderWidth: 4, borderRadius: 5 },
        label: { show: false },
        emphasis: { scaleSize: 6 },
        data: items,
      },
    ],
  });

  document.getElementById("pile-state-legend").innerHTML = items
    .map((item) => `<span class="legend-item"><i style="background:${item.color}"></i>${item.name}<strong>${item.value}</strong></span>`)
    .join("");
}

function renderResourceUtilization(pileStates, summary) {
  const total = summary.pileCount || 0;
  const availableRate = percentage(pileStates.idle, total);
  const faultRate = percentage(pileStates.fault, total);
  setText("availability-rate", `${availableRate.toFixed(1)}%`);
  setText("fault-rate", `${faultRate.toFixed(1)}%`);
  setText(
    "piles-per-station",
    summary.stationCount ? (total / summary.stationCount).toFixed(1) : "--",
  );
  document.getElementById("availability-ring").style.setProperty("--score-angle", `${availableRate * 3.6}deg`);

  const rows = [
    { label: "闲置", value: pileStates.idle, color: COLORS.green },
    { label: "在用", value: pileStates.inUse, color: COLORS.blue },
    { label: "异常", value: pileStates.fault, color: COLORS.red },
  ];
  document.getElementById("state-progress-list").innerHTML = rows
    .map((item) => {
      const rate = percentage(item.value, total);
      return `<div class="progress-row">
        <span>${item.label}</span>
        <span class="progress-track"><i style="--progress:${rate}%;--bar-color:${item.color}"></i></span>
        <strong>${rate.toFixed(0)}%</strong>
      </div>`;
    })
    .join("");
}

function renderRevenue(allPoints) {
  const count = Math.min(state.revenueDays, allPoints.length);
  const points = allPoints.slice(-count);
  const values = points.map((point) => point.revenueCents / 100);
  const maxValue = Math.max(...values, 0);

  const chart = chartFor("revenue-chart");
  chart.setOption(
    {
      animationDuration: 650,
      animationEasing: "cubicOut",
      grid: { left: 58, right: 28, top: 35, bottom: 44 },
      tooltip: {
        trigger: "axis",
        axisPointer: { type: "line", lineStyle: { color: "rgba(33, 104, 232, 0.24)" } },
        backgroundColor: "rgba(22, 47, 74, 0.95)",
        borderWidth: 0,
        padding: [9, 11],
        textStyle: { color: "#fff", fontSize: 11 },
        formatter: (params) => {
          const point = points[params[0].dataIndex];
          return `${point.date}<br><strong>${formatMoney(point.revenueCents)}</strong>`;
        },
      },
      xAxis: {
        type: "category",
        boundaryGap: true,
        data: points.map((point) => shortDate(point.date)),
        axisLine: { lineStyle: { color: "#dce6f0" } },
        axisTick: { show: false },
        axisLabel: {
          color: COLORS.axis,
          fontSize: 9,
          interval: count > 14 ? 4 : 0,
          margin: 13,
        },
      },
      yAxis: {
        type: "value",
        name: "营收（元）",
        nameTextStyle: { color: COLORS.axis, fontSize: 9, padding: [0, 0, 5, -18] },
        min: 0,
        max: maxValue === 0 ? 10 : undefined,
        splitNumber: 4,
        axisLabel: { color: COLORS.axis, fontSize: 9 },
        splitLine: { lineStyle: { color: COLORS.grid, type: "dashed" } },
      },
      series: [
        {
          name: "日营收",
          type: "bar",
          data: values,
          barMaxWidth: count === 7 ? 38 : 16,
          itemStyle: {
            borderRadius: [5, 5, 1, 1],
            color: new window.echarts.graphic.LinearGradient(0, 0, 0, 1, [
              { offset: 0, color: "#16bad3" },
              { offset: 1, color: "rgba(47, 111, 235, 0.4)" },
            ]),
          },
          emphasis: { itemStyle: { color: COLORS.blue } },
        },
        {
          name: "营收趋势",
          type: "line",
          data: values,
          smooth: 0.34,
          symbol: "circle",
          symbolSize: count === 7 ? 7 : 4,
          lineStyle: { width: 2.5, color: COLORS.blue },
          itemStyle: { color: COLORS.blue, borderColor: "#fff", borderWidth: 2 },
          areaStyle: {
            color: new window.echarts.graphic.LinearGradient(0, 0, 0, 1, [
              { offset: 0, color: "rgba(33, 104, 232, 0.18)" },
              { offset: 1, color: "rgba(33, 104, 232, 0.01)" },
            ]),
          },
          z: 3,
        },
      ],
    },
    true,
  );
  chart.getDom().setAttribute("aria-label", `近${count}日营收趋势图`);

  const totalCents = points.reduce((sum, point) => sum + point.revenueCents, 0);
  const peakPoint = points.reduce(
    (peak, point) => (point.revenueCents > peak.revenueCents ? point : peak),
    points[0],
  );
  setText("range-revenue", formatMoney(totalCents));
  setText("average-revenue", formatMoney(Math.round(totalCents / points.length)));
  setText("peak-date", shortDate(peakPoint.date));
  setText("peak-revenue", formatMoney(peakPoint.revenueCents));
}

function renderForecast(predictions) {
  const prediction = predictions[0];
  const content = document.querySelector(".forecast-content");
  const empty = document.getElementById("forecast-empty");
  if (!prediction || !Array.isArray(prediction.points) || prediction.points.length === 0) {
    content.hidden = true;
    empty.hidden = false;
    setText("prediction-source", "暂无数据");
    return;
  }

  content.hidden = false;
  empty.hidden = true;
  const points = prediction.points;
  const congestion = congestionMeta(prediction.congestionLevel);
  const latestPoint = points.at(-1);

  setText("forecast-subtitle", `代表站点 #${prediction.stationId} · ${prediction.source} 预测`);
  setText("forecast-horizon", `未来 ${prediction.horizonHours} 小时`);
  setText("prediction-source", prediction.source);
  setText("forecast-load", `${formatDecimal(latestPoint.loadKw)} kW`);
  setText("forecast-available", `${latestPoint.availablePiles} 个`);
  setText("forecast-time", formatForecastTime(latestPoint.time));

  const status = document.getElementById("forecast-status");
  status.classList.remove("is-medium", "is-high");
  if (prediction.congestionLevel === "MEDIUM") status.classList.add("is-medium");
  if (prediction.congestionLevel === "HIGH") status.classList.add("is-high");
  status.querySelector("strong").textContent = congestion.label;

  const chart = chartFor("forecast-chart");
  chart.setOption({
    animationDuration: 650,
    grid: { left: 50, right: 48, top: 34, bottom: 37 },
    legend: {
      top: 8,
      right: 8,
      itemWidth: 8,
      itemHeight: 8,
      textStyle: { color: COLORS.axis, fontSize: 9 },
      data: ["预测负荷", "可用电桩"],
    },
    tooltip: {
      trigger: "axis",
      backgroundColor: "rgba(22, 47, 74, 0.95)",
      borderWidth: 0,
      textStyle: { color: "#fff", fontSize: 10 },
    },
    xAxis: {
      type: "category",
      data: points.map((point) => formatForecastTime(point.time)),
      axisTick: { show: false },
      axisLine: { lineStyle: { color: "#dce6f0" } },
      axisLabel: { color: COLORS.axis, fontSize: 9 },
    },
    yAxis: [
      {
        type: "value",
        name: "负荷 kW",
        min: 0,
        nameTextStyle: { color: COLORS.axis, fontSize: 8 },
        axisLabel: { color: COLORS.axis, fontSize: 8 },
        splitLine: { lineStyle: { color: COLORS.grid, type: "dashed" } },
      },
      {
        type: "value",
        name: "电桩",
        min: 0,
        minInterval: 1,
        nameTextStyle: { color: COLORS.axis, fontSize: 8 },
        axisLabel: { color: COLORS.axis, fontSize: 8 },
        splitLine: { show: false },
      },
    ],
    series: [
      {
        name: "预测负荷",
        type: "line",
        smooth: true,
        symbolSize: 8,
        data: points.map((point) => point.loadKw),
        lineStyle: { width: 2.5, color: COLORS.blue },
        itemStyle: { color: COLORS.blue, borderColor: "#fff", borderWidth: 2 },
        areaStyle: {
          color: new window.echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: "rgba(33, 104, 232, 0.2)" },
            { offset: 1, color: "rgba(33, 104, 232, 0.01)" },
          ]),
        },
      },
      {
        name: "可用电桩",
        type: "bar",
        yAxisIndex: 1,
        barMaxWidth: 24,
        data: points.map((point) => point.availablePiles),
        itemStyle: {
          borderRadius: [5, 5, 1, 1],
          color: new window.echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: COLORS.green },
            { offset: 1, color: "rgba(22, 185, 137, 0.38)" },
          ]),
        },
      },
    ],
  });
}

function renderHealthGauge(pileStates, pileCount) {
  const availability = percentage(pileStates.idle, pileCount);
  const health = availability >= 50
    ? { title: "资源充足", description: "当前闲置电桩占比较高", color: COLORS.green }
    : availability >= 25
      ? { title: "资源适中", description: "建议关注高峰时段", color: COLORS.amber }
      : { title: "资源紧张", description: "当前可用电桩偏少", color: COLORS.red };

  setText("health-title", health.title);
  setText("health-description", health.description);
  const chart = chartFor("health-gauge-chart");
  chart.setOption({
    series: [
      {
        type: "gauge",
        startAngle: 205,
        endAngle: -25,
        center: ["50%", "61%"],
        radius: "91%",
        min: 0,
        max: 100,
        progress: { show: true, roundCap: true, width: 12, itemStyle: { color: health.color } },
        axisLine: { roundCap: true, lineStyle: { width: 12, color: [[1, "#e9eff5"]] } },
        axisTick: { show: false },
        splitLine: { show: false },
        axisLabel: { show: false },
        pointer: { show: false },
        anchor: { show: false },
        title: { offsetCenter: [0, "35%"], color: COLORS.axis, fontSize: 9 },
        detail: {
          valueAnimation: true,
          offsetCenter: [0, "-3%"],
          color: "#264d71",
          fontSize: 24,
          fontWeight: 750,
          formatter: "{value}%",
        },
        data: [{ value: Number(availability.toFixed(1)), name: "电桩可用率" }],
      },
    ],
  });
}

function renderRevenueBars(summary) {
  const items = [
    { label: "今日", value: summary.todayRevenueCents, color: COLORS.green },
    { label: "本月", value: summary.monthRevenueCents, color: COLORS.blue },
    { label: "累计", value: summary.totalRevenueCents, color: COLORS.amber },
  ];
  const max = Math.max(...items.map((item) => item.value), 1);
  document.getElementById("revenue-bars").innerHTML = items
    .map((item) => {
      const width = item.value === 0 ? 0 : Math.max(5, (item.value / max) * 100);
      return `<div class="revenue-bar-row">
        <span>${item.label}</span>
        <span class="revenue-bar-track"><i style="--progress:${width}%;--bar-color:${item.color}"></i></span>
        <strong>${formatMoney(item.value)}</strong>
      </div>`;
    })
    .join("");
}

function chartFor(id) {
  if (!window.echarts) {
    throw new Error("ECharts 未加载，请检查网络或改用本地 ECharts 文件");
  }
  if (!state.charts.has(id)) {
    const element = document.getElementById(id);
    state.charts.set(id, window.echarts.init(element, null, { renderer: "canvas" }));
  }
  return state.charts.get(id);
}

function resizeCharts() {
  state.charts.forEach((chart) => chart.resize());
}

function formatMoney(cents) {
  return moneyFormatter.format((Number(cents) || 0) / 100);
}

function formatTimestamp(value) {
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? "--" : dateTimeFormatter.format(date).replaceAll("/", "-");
}

function formatForecastTime(value) {
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? "--" : forecastTimeFormatter.format(date).replaceAll("/", "-");
}

function shortDate(value) {
  if (typeof value !== "string") return "--";
  const [, month = "--", day = "--"] = value.split("-");
  return `${month}-${day}`;
}

function formatDecimal(value) {
  return Number.isFinite(value) ? new Intl.NumberFormat("zh-CN", { maximumFractionDigits: 1 }).format(value) : "--";
}

function percentage(value, total) {
  return total > 0 ? (Number(value) / Number(total)) * 100 : 0;
}

function congestionMeta(level) {
  return {
    LOW: { label: "低拥堵" },
    MEDIUM: { label: "中等拥堵" },
    HIGH: { label: "高拥堵" },
  }[level] || { label: "未知" };
}

function setText(id, text) {
  document.getElementById(id).textContent = text;
}

function hideLoading() {
  document.getElementById("loading-overlay").classList.add("is-hidden");
}

function showError(message) {
  const banner = document.getElementById("error-banner");
  setText("error-message", message);
  banner.hidden = false;
}

function clearError() {
  document.getElementById("error-banner").hidden = true;
}

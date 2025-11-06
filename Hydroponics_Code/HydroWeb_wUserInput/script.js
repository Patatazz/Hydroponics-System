// =========================
// MERGED Hydroponic System Script
// Dashboard: OLD logic preserved
// Scheduling/Analytics/Notifications: NEW logic integrated
// Manual Injection: NEW user input functionality
// =========================

// Firebase Initialization
const firebaseConfig = {
  apiKey: "AIzaSyDzXywrMU5Wsx_ylC925U_-TbUYgOsIAv8",
  authDomain: "hydroponic-4672a.firebaseapp.com",
  databaseURL: "https://hydroponic-4672a-default-rtdb.firebaseio.com",
  projectId: "hydroponic-4672a",
  storageBucket: "hydroponic-4672a.appspot.com",
  messagingSenderId: "573052535206",
  appId: "1:573052535206:web:bc4a5f28922d51e0200f67",
  measurementId: "G-NSZE4MMCP2",
};

if (!firebase.apps || !firebase.apps.length) {
  firebase.initializeApp(firebaseConfig);
}
const database = firebase.database();
const auth = firebase.auth();

// Authentication
const USER_EMAIL = "irishstephany03@gmail.com";
const USER_PASSWORD = "#Kawal12345";
auth.signInWithEmailAndPassword(USER_EMAIL, USER_PASSWORD)
  .then(() => {
    console.log("✓ Signed in to Firebase with email");
    startFirebaseListeners();
  })
  .catch((error) => {
    console.error("Firebase Auth Error:", error);
    logEvent("Failed to authenticate with Firebase", "bad");
  });

// Crop targets (from OLD file)
const crops = {
  lettuce: { N: 100, P: 25.0, K:125},
  bokchoy: { N: 180, P: 45, K: 220}
};

// Application state (from OLD file)
const state = {
  systemOn: true,
  modeAuto: true,
  crop: "lettuce",
  sensors: { water_level: 0 },
  actuators: { pump: false, aerator: false },
  readingsHistory: [],
  logs: [],
  schedule: {
    npkInjection: { time: "06:15", executedToday: false },
    pumpCycles: [
      { start: "06:15", duration: 20, active: false },
      { start: "12:00", duration: 20, active: false },
    ],
  },
  npkData: { N: 0, P: 0, K: 0 },
};

let nitrogenChart, phosphorusChart, potassiumChart;

// DOM references (OLD + NEW combined)
const app = document.getElementById("app");
const splash = document.getElementById("splash");

const navButtons = document.querySelectorAll(".nav-btn");
const sidebarToggleBtn = document.getElementById("sidebar-toggle-btn");
const mobileMenuBtn = document.getElementById("mobile-menu-btn");

const systemPowerToggle = document.getElementById("system-power-toggle");
const systemStatusText = document.getElementById("system-status-text");
const cropSelect = document.getElementById("crop-select");
const modeToggle = document.getElementById("mode-toggle");
const modeLabel = document.getElementById("mode-label");

const tankFill = document.getElementById("tank-fill");
const waterValueEl = document.getElementById("water-value");
const waterTargetEl = document.getElementById("water-target");
const waterStatusEl = document.getElementById("water-status");
const waterRawValueEl = document.getElementById("water-raw-value");

const pumpOnBtn = document.getElementById("pump-on-btn");
const pumpOffBtn = document.getElementById("pump-off-btn");

// NEW: Manual injection input elements
const nAmountInput = document.getElementById("n-amount-input");
const pAmountInput = document.getElementById("p-amount-input");
const kAmountInput = document.getElementById("k-amount-input");
const injectNBtn = document.getElementById("inject-n-btn");
const injectPBtn = document.getElementById("inject-p-btn");
const injectKBtn = document.getElementById("inject-k-btn");
const injectAllBtn = document.getElementById("inject-all-btn");

const dashboardView = document.getElementById("dashboard-view");
const schedulingView = document.getElementById("scheduling-view");
const analyticsView = document.getElementById("analytics-view");
const notificationsView = document.getElementById("notifications-view");
const logList = document.getElementById("log-list");
const npkScheduleDetails = document.getElementById("npk-schedule-details");
const pumpScheduleDetails = document.getElementById("pump-schedule-details");

// NEW: Scheduling view mode selector
const modeSelect = document.getElementById("mode-select");

// ==================== HELPER: LOG NPK INJECTION TO FIREBASE ====================

function logNPKInjectionToFirebase(mode, nValue, pValue, kValue, additionalInfo = {}) {
  const now = new Date();
  const logEntry = {
    action: "NPK Injection",
    date: now.toLocaleDateString(),
    time: now.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }),
    mode: mode, // "Manual" or "Automatic"
    N: nValue || 0,
    P: pValue || 0,
    K: kValue || 0,
    timestamp: firebase.database.ServerValue.TIMESTAMP,
    ...additionalInfo // Can include crop, source, etc.
  };
  
  database.ref("logs").push(logEntry)
    .then(() => {
      console.log(`NPK Injection logged to Firebase (${mode})`);
    })
    .catch((error) => {
      console.error("Failed to log NPK injection:", error);
      logEvent(`Failed to log ${mode} injection: ${error.message}`, "bad");
    });
}

// ==================== OLD DASHBOARD FUNCTIONS ====================

function initializeChart(sensor, color) {
  const ctx = document
    .getElementById(`${sensor}DoughnutChart`)
    ?.getContext("2d");
  if (!ctx) {
    console.error(`Chart canvas not found for sensor: ${sensor}`);
    return null;
  }
  return new Chart(ctx, {
    type: "doughnut",
    data: {
      datasets: [
        {
          data: [0, 100],
          backgroundColor: [color, "rgba(45, 212, 191, 0.1)"],
          borderWidth: 0,
        },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      cutout: "80%",
      plugins: {
        legend: { display: false },
        tooltip: { enabled: false },
      },
      layout: {
        padding: 0,
      },
    },
  });
}

function updateChartAndStatus(sensor, value, target, chartInstance) {
  const maxRange = target * 1.5;
  const valueEl = document.getElementById(`${sensor}-value`);
  const targetEl = document.getElementById(`${sensor}-target`);
  const statusEl = document.getElementById(`${sensor}-status`);

  if (valueEl) valueEl.textContent = `${Number(value).toFixed(1)} ppm`;
  if (targetEl) targetEl.textContent = `${target} ppm`;

  const diff = Math.abs(value - target);
  const tolerance = target * 0.15;
  if (statusEl) statusEl.className = "status-dot";

  let color;
  if (value < 10) {
    if (statusEl) statusEl.classList.add("bad");
    color = "var(--red)";
  } else if (diff > tolerance) {
    if (statusEl) statusEl.classList.add("warn-dot");
    color = "var(--amber)";
  } else {
    if (statusEl) statusEl.classList.add("ok");
    color = "var(--green)";
  }

  if (chartInstance) {
    chartInstance.data.datasets[0].data = [
      value,
      Math.max(0, maxRange - value),
    ];
    chartInstance.data.datasets[0].backgroundColor = [
      color,
      "rgba(45, 212, 191, 0.1)",
    ];
    chartInstance.update();
  }
}

function updateWaterGauge(level, target) {
  if (tankFill) tankFill.style.height = `${Number(level).toFixed(0)}%`;
  if (waterValueEl) waterValueEl.textContent = `${Number(level).toFixed(0)}%`;
  if (waterTargetEl) waterTargetEl.textContent = `${target}%`;

  if (waterRawValueEl) {
    waterRawValueEl.textContent = `${Number(level).toFixed(1)}%`;
  }

  if (waterStatusEl) {
    waterStatusEl.className = "status-dot";
    if (level < target - 15) {
      waterStatusEl.classList.add("bad");
      waterStatusEl.textContent = "LOW";
    } else if (level < target - 5) {
      waterStatusEl.classList.add("ok");
      waterStatusEl.textContent = "ATTENTION";
    } else {
      waterStatusEl.classList.add("ok");
      waterStatusEl.textContent = "OK";
    }
  }
}

function logEvent(message, type = "ok") {
  const now = new Date();
  const time = now.toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
  const entry = { time, message, type };
  state.logs.unshift(entry);
  if (state.logs.length > 50) state.logs.pop();
}

function renderSchedule() {
  const npkTime = state.schedule.npkInjection.time;
  if (npkScheduleDetails) npkScheduleDetails.innerHTML = `
      <div class="schedule-item">
          <h5>Daily Injection</h5>
          <p><strong>Time:</strong> ${npkTime}</p>
          <p><strong>Next Run:</strong> ${
            state.schedule.npkInjection.executedToday ? "Tomorrow" : "Today"
          }</p>
          <p><strong>Status:</strong> ${
            state.schedule.npkInjection.executedToday ? "Executed" : "Pending"
          }</p>
      </div>
    `;

  if (pumpScheduleDetails) pumpScheduleDetails.innerHTML = state.schedule.pumpCycles
    .map((cycle, index) => {
      return `
            <div class="schedule-item">
                <h5>Pump Cycle ${index + 1}</h5>
                <p><strong>Start Time:</strong> ${cycle.start}</p>
                <p><strong>Duration:</strong> ${cycle.duration} minutes</p>
                <p><strong>Status:</strong> ${cycle.active ? "Running" : "Scheduled"}</p>
            </div>
        `;
    })
    .join("");
}

function render() {
  const target = crops[state.crop];

  if (nitrogenChart)
    updateChartAndStatus("N", state.npkData.N, target.N, nitrogenChart);
  if (phosphorusChart)
    updateChartAndStatus("P", state.npkData.P, target.P, phosphorusChart);
  if (potassiumChart)
    updateChartAndStatus("K", state.npkData.K, target.K, potassiumChart);

  updateWaterGauge(state.sensors.water_level, target.water);

  if (systemStatusText) systemStatusText.textContent = state.systemOn ? "System ON" : "System OFF";

  if (modeLabel) modeLabel.textContent = state.modeAuto ? "Auto" : "Manual";

  const isDisabled = !state.systemOn || state.modeAuto;
  if (pumpOnBtn) pumpOnBtn.disabled = isDisabled;
  if (pumpOffBtn) pumpOffBtn.disabled = isDisabled;

  // Update manual injection button states based on system state
  updateManualInjectionButtons();

  if (logList) logList.innerHTML = state.logs
    .map((log) => `<li class="${log.type}">[${log.time}] ${log.message}</li>`)
    .join("");

  renderSchedule();

  if (!isDisabled) {
    if (pumpOnBtn) pumpOnBtn.classList.toggle("primary", state.actuators.pump);
    if (pumpOffBtn) pumpOffBtn.classList.toggle("warn", !state.actuators.pump);
  } else {
    if (pumpOnBtn) pumpOnBtn.classList.remove("primary");
    if (pumpOffBtn) pumpOffBtn.classList.remove("warn");
  }
}

function captureReading() {
  if (!state.systemOn) return;

  const now = new Date();
  const timestamp = now.toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
  const reading = {
    timestamp,
    N: Number(state.npkData.N).toFixed(1),
    P: Number(state.npkData.P).toFixed(1),
    K: Number(state.npkData.K).toFixed(1),
    water: Number(state.sensors.water_level).toFixed(1),
    pump: state.actuators.pump,
  };
  state.readingsHistory.unshift(reading);
  if (state.readingsHistory.length > 50) state.readingsHistory.pop();
}

function evaluateAlerts() {
  if (!state.systemOn) return;

  const target = crops[state.crop];
  const threshold = 0.2;
  let alerts = 0;

  ["N", "P", "K"].forEach((key) => {
    const value = state.npkData[key];
    const t = target[key];
    const diff = Math.abs(value - t) / t;

    if (value < 5) {
      if (Math.random() < 0.05) {
        logEvent(
          `${key} LEVEL CRITICAL: Value dropped to ${Number(value).toFixed(1)} ppm.`,
          "bad"
        );
      }
      alerts++;
    } else if (diff > threshold) {
      const status = value > t ? "HIGH" : "LOW";
      if (Math.random() < 0.02) {
        logEvent(
          `${key} LEVEL ${status}: Value is ${Number(value).toFixed(1)} ppm (Target: ${t} ppm).`,
          "warn"
        );
      }
      alerts++;
    }
  });

  if (state.sensors.water_level < target.water - 10) {
    if (Math.random() < 0.05) {
      logEvent(
        `WATER LEVEL LOW: Current level ${Number(state.sensors.water_level).toFixed(1)}% (Target: ${target.water}%).`,
        "bad"
      );
    }
    alerts++;
  }

  if (alerts === 0 && Math.random() < 0.01) {
    logEvent("System health check: All parameters nominal.", "ok");
  }
}

function scheduleCheck() {
  if (!state.systemOn || !state.modeAuto) return;

  const now = new Date();
  const currentMinutes = now.getHours() * 60 + now.getMinutes();

  const npkSchedule = state.schedule.npkInjection;
  const [npkHour, npkMinute] = npkSchedule.time.split(":").map(Number);
  const npkScheduledMinutes = npkHour * 60 + npkMinute;

  if (currentMinutes === npkScheduledMinutes && !npkSchedule.executedToday) {
    database.ref("controls/autoDoseTrigger").set(firebase.database.ServerValue.TIMESTAMP);
    logEvent("AUTOMATED: Daily NPK injection triggered.", "ok");
    npkSchedule.executedToday = true;

    // Log automatic injection to Firebase for analytics
    const target = crops[state.crop];
    logNPKInjectionToFirebase("Automatic", target.N, target.P, target.K, { 
      crop: state.crop,
      source: "scheduled"
    });
  }

  if (now.getHours() === 0 && now.getMinutes() === 0) {
    npkSchedule.executedToday = false;
  }

  state.schedule.pumpCycles.forEach((cycle) => {
    const [startHour, startMinute] = cycle.start.split(":").map(Number);
    const startMinutes = startHour * 60 + startMinute;
    const endMinutes = startMinutes + cycle.duration;

    const isActive =
      currentMinutes >= startMinutes && currentMinutes < endMinutes;

    if (isActive && !cycle.active) {
      cycle.active = true;
      handlePumpControl(true, true);
      logEvent(
        `AUTOMATED: Pump cycle started at ${cycle.start} for ${cycle.duration} min.`,
        "ok"
      );
    } else if (!isActive && cycle.active) {
      cycle.active = false;
      handlePumpControl(false, true);
      logEvent("AUTOMATED: Pump cycle completed.", "ok");
    }
  });
}

function simulationTick() {
  render();
  if (state.systemOn) {
    scheduleCheck();
    captureReading();
    evaluateAlerts();
  }
}

// ==================== FIREBASE LISTENERS ====================

let firebaseListenersStarted = false;
function startFirebaseListeners() {
  if (firebaseListenersStarted) return;
  firebaseListenersStarted = true;

  database.ref("sensors").on(
    "value",
    (snapshot) => {
      const data = snapshot.val();
      if (data) {
        state.npkData.N = data.N || 0;
        state.npkData.P = data.P || 0;
        state.npkData.K = data.K || 0;
        render();
      }
    },
    (error) => {
      logEvent(`Firebase NPK Read Error: ${error.code}`, "bad");
    }
  );

  database.ref("sensors/water_level").on(
    "value",
    (snapshot) => {
      const level = snapshot.val();
      if (typeof level === "number" || !isNaN(Number(level))) {
        state.sensors.water_level = Number(level);
        render();
      }
    },
    (error) => {
      logEvent(`Firebase Water Level Read Error: ${error.code}`, "bad");
    }
  );

  database.ref("actuators/pump").on("value", (snapshot) => {
    const val = snapshot.val();
    const isPumpOn = val === 1 || val === true;
    state.actuators.pump = isPumpOn;
    render();
  });

  // Start manual injection status listener
  startManualInjectionListener();
}

// ==================== CONTROL FUNCTIONS ====================

function handlePumpControl(isOn, isAutomated = false) {
  if (!state.systemOn) {
    logEvent("Pump control denied: System is OFF.", "bad");
    return;
  }

  if (!isAutomated && state.modeAuto) {
    logEvent("Manual Pump control denied: System is in Auto mode.", "warn");
    return;
  }

  const pumpAction = isOn ? 1 : 0;
  database
    .ref("controls/pump")
    .set(pumpAction)
    .then(() => {
      const source = isAutomated ? "Automated System" : "Manual Control";
      const action = isOn ? "ON" : "OFF";
      const type = isOn ? "ok" : "warn";
      logEvent(`${source}: Water Pump turned ${action} (Command sent).`, type);
    })
    .catch((error) => {
      logEvent(
        `Failed to write pump command to Firebase: ${error.message}`,
        "bad"
      );
    });
}

// ==================== NEW: MANUAL INJECTION FUNCTIONS ====================

function updateManualInjectionButtons() {
  const canInject = state.systemOn && !state.modeAuto;
  
  if (injectNBtn) injectNBtn.disabled = !canInject;
  if (injectPBtn) injectPBtn.disabled = !canInject;
  if (injectKBtn) injectKBtn.disabled = !canInject;
  if (injectAllBtn) injectAllBtn.disabled = !canInject;
}

function handleManualNutrientInjection(nutrient, amount) {
  if (!state.systemOn) {
    logEvent(`Manual ${nutrient} injection denied: System is OFF.`, "bad");
    return;
  }

  if (state.modeAuto) {
    logEvent(`Manual ${nutrient} injection denied: System is in Auto mode.`, "warn");
    return;
  }

  const ppmValue = parseFloat(amount);
  if (isNaN(ppmValue) || ppmValue <= 0) {
    logEvent(`Invalid ${nutrient} PPM value. Please enter a valid number.`, "warn");
    return;
  }

  database.ref("controls/manualInjection").once("value").then((snapshot) => {
    const existing = snapshot.val();

    if (existing) {
      const anyActive = ["N", "P", "K"].some(n => existing[n]?.active);
      if (anyActive || existing.batchInjection) {
        logEvent(
          `Cannot start ${nutrient} injection: Another injection is in progress.`,
          "warn"
        );
        return;
      }
    }

    // Disable only the clicked button
    const button = document.getElementById(`inject-${nutrient.toLowerCase()}-btn`);
    if (button) {
      button.disabled = true;
      button.classList.add("active-inject");
      button.innerHTML = `<i class="fas fa-spinner fa-spin"></i> Injecting...`;
    }

    const injectionData = {
      [nutrient]: { targetPPM: ppmValue, active: true },
      timestamp: firebase.database.ServerValue.TIMESTAMP,
      batchInjection: false,
      source: "single"
    };

    database.ref("controls/manualInjection").set(injectionData)
      .then(() => {
        logEvent(`Manual ${nutrient} injection started (${ppmValue} PPM).`, "ok");

        // Log to Firebase for analytics
        const nVal = nutrient === "N" ? ppmValue : 0;
        const pVal = nutrient === "P" ? ppmValue : 0;
        const kVal = nutrient === "K" ? ppmValue : 0;
        logNPKInjectionToFirebase("Manual", nVal, pVal, kVal, { source: "single" });
      })
      .catch((error) => {
        logEvent(`Failed to start ${nutrient} injection: ${error.message}`, "bad");
        if (button) {
          button.disabled = false;
          button.classList.remove("active-inject");
          button.innerHTML = `<i class="fas fa-syringe"></i> Inject ${nutrient}`;
        }
      });
  });
}


function handleInjectAll() {
  if (!state.systemOn) {
    logEvent("Inject All denied: System is OFF.", "bad");
    return;
  }

  if (state.modeAuto) {
    logEvent("Inject All denied: System is in Auto mode.", "warn");
    return;
  }

  const nValue = parseFloat(nAmountInput.value) || 0;
  const pValue = parseFloat(pAmountInput.value) || 0;
  const kValue = parseFloat(kAmountInput.value) || 0;

  if (nValue <= 0 && pValue <= 0 && kValue <= 0) {
    logEvent("Please enter at least one valid nutrient PPM value.", "warn");
    return;
  }

  database.ref("controls/manualInjection").once("value").then((snapshot) => {
    const existing = snapshot.val();
    if (existing) {
      const anyActive = ["N", "P", "K"].some(n => existing[n]?.active);
      if (anyActive || existing.batchInjection) {
        logEvent("Cannot start injection: Another injection is in progress.", "warn");
        return;
      }
    }

    injectAllBtn.disabled = true;
    injectAllBtn.classList.add("active-inject");
    injectAllBtn.innerHTML = `<i class="fas fa-spinner fa-spin"></i> Injecting...`;

    const batchData = {
      timestamp: firebase.database.ServerValue.TIMESTAMP,
      batchInjection: true,
      source: "batch"
    };
    if (nValue > 0) batchData.N = { targetPPM: nValue, active: true };
    if (pValue > 0) batchData.P = { targetPPM: pValue, active: true };
    if (kValue > 0) batchData.K = { targetPPM: kValue, active: true };

    database.ref("controls/manualInjection").set(batchData)
      .then(() => {
        logEvent("Batch Injection started for selected nutrients.", "ok");
        
        // Log to Firebase for analytics
        logNPKInjectionToFirebase("Manual", nValue, pValue, kValue, { source: "batch" });
      })
      .catch((err) => {
        logEvent("Failed to start batch injection: " + err.message, "bad");
        injectAllBtn.disabled = false;
        injectAllBtn.classList.remove("active-inject");
        injectAllBtn.innerHTML = `<i class="fas fa-fill-drip"></i> Inject All Nutrients`;
      });
  });
}

function startManualInjectionListener() {
  // Listen to the manualInjection path for button state management
  database.ref("controls/manualInjection").on("value", (snapshot) => {
    const injectionData = snapshot.val();
    
    if (!injectionData) {
      // Reset all buttons when no active injection
      resetAllInjectionButtons();
      return;
    }

    // Check individual nutrient injections from manualInjection path
    ["N", "P", "K"].forEach(nutrient => {
      const nutData = injectionData[nutrient];
      const button = document.getElementById(`inject-${nutrient.toLowerCase()}-btn`);
      
      if (nutData && nutData.active && button) {
        button.disabled = true;
        button.classList.add("active-inject");
        button.innerHTML = `<i class="fas fa-spinner fa-spin"></i> Injecting...`;
      }
    });

    // Check batch injection
    if (injectionData.batchInjection && injectAllBtn) {
      injectAllBtn.disabled = true;
      injectAllBtn.classList.add("active-inject");
      injectAllBtn.innerHTML = `<i class="fas fa-spinner fa-spin"></i> Injecting All...`;
    }
  });

  // NEW: Also listen to the dose path (where ESP32 writes completion status)
  database.ref("controls/dose").on("value", (snapshot) => {
    const doseData = snapshot.val();
    
    if (!doseData) {
      resetAllInjectionButtons();
      return;
    }

    // Check each nutrient's dose status
    ["N", "P", "K"].forEach(nutrient => {
      const isDosing = doseData[nutrient];
      const button = document.getElementById(`inject-${nutrient.toLowerCase()}-btn`);
      
      if (!button) return;

      if (isDosing === true) {
        // Dosing is active
        button.disabled = true;
        button.classList.add("active-inject");
        button.innerHTML = `<i class="fas fa-spinner fa-spin"></i> Injecting...`;
      } else if (isDosing === false) {
        // Dosing completed
        const canInject = state.systemOn && !state.modeAuto;
        button.disabled = !canInject;
        button.classList.remove("active-inject");
        button.innerHTML = `<i class="fas fa-syringe"></i> Inject ${nutrient}`;
        
        // Clear the manualInjection data for this nutrient
        database.ref(`controls/manualInjection/${nutrient}`).remove();
      }
    });

    // Check if all nutrients are done (for Inject All)
    const allDone = ["N", "P", "K"].every(n => doseData[n] === false || doseData[n] === undefined);
    if (allDone && injectAllBtn) {
      const canInject = state.systemOn && !state.modeAuto;
      injectAllBtn.disabled = !canInject;
      injectAllBtn.classList.remove("active-inject");
      injectAllBtn.innerHTML = `<i class="fas fa-fill-drip"></i> Inject All Nutrients`;
      
      // Clear batch injection flag
      database.ref("controls/manualInjection/batchInjection").remove();
    }
  });
}

// Helper function to reset all injection buttons
function resetAllInjectionButtons() {
  const canInject = state.systemOn && !state.modeAuto;
  
  if (injectNBtn) {
    injectNBtn.disabled = !canInject;
    injectNBtn.classList.remove("active-inject");
    injectNBtn.innerHTML = `<i class="fas fa-syringe"></i> Inject N`;
  }
  if (injectPBtn) {
    injectPBtn.disabled = !canInject;
    injectPBtn.classList.remove("active-inject");
    injectPBtn.innerHTML = `<i class="fas fa-syringe"></i> Inject P`;
  }
  if (injectKBtn) {
    injectKBtn.disabled = !canInject;
    injectKBtn.classList.remove("active-inject");
    injectKBtn.innerHTML = `<i class="fas fa-syringe"></i> Inject K`;
  }
  if (injectAllBtn) {
    injectAllBtn.disabled = !canInject;
    injectAllBtn.classList.remove("active-inject");
    injectAllBtn.innerHTML = `<i class="fas fa-fill-drip"></i> Inject All Nutrients`;
  }
}

// ==================== NEW: SCHEDULING VIEW LOGIC ====================

function displayAllLogs(data) {
  const npkDiv = document.getElementById("npk-schedule-details");
  const pumpDiv = document.getElementById("pump-schedule-details");
  if (npkDiv) npkDiv.innerHTML = "";
  if (pumpDiv) pumpDiv.innerHTML = "";

  let npkCycle = 1;
  let pumpCycle = 1;

  Object.entries(data).forEach(([id, log]) => {
    const { action, date, time } = log;
    const duration = log.duration || "20 mins";

    let endTime = "";
    if (time && log.endTime) {
      endTime = log.endTime;
    } else if (time) {
      const [h, m] = time.split(":").map(Number);
      const d = new Date(2025, 0, 1, h, m + 20);
      endTime = `${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`;
    }

    const html = `
      <div class="schedule-item">
        <div><strong>${action === "NPK Injection" ? "Cycle " + npkCycle++ : "Cycle " + pumpCycle++}</strong> - ${action}</div>
        <div>Date: ${date || "N/A"}</div>
        <div>Start: ${time || "N/A"}</div>
        <div>End: ${endTime || "N/A"}</div>
        <div>Duration: ${duration}</div>
      </div>
      <hr>
    `;

    if (action === "NPK Injection" && npkDiv) npkDiv.innerHTML += html;
    if (action === "Water Pump" && pumpDiv) pumpDiv.innerHTML += html;
  });
}

function displayManualLogs() {
  const manualRef = database.ref("Manual");
  const npkDiv = document.getElementById("npk-schedule-details");
  const pumpDiv = document.getElementById("pump-schedule-details");

  if (npkDiv) npkDiv.innerHTML = "";
  if (pumpDiv) pumpDiv.innerHTML = "";

  manualRef
    .once("value")
    .then((snapshot) => {
      const data = snapshot.val();
      if (!data) {
        if (npkDiv) npkDiv.innerHTML = "<p>No manual logs found.</p>";
        if (pumpDiv) pumpDiv.innerHTML = "<p>No manual logs found.</p>";
        return;
      }

      let npkCycle = 1;
      let pumpCycle = 1;

      Object.entries(data).forEach(([key, log]) => {
        const action = log.action || log.device || "Unknown";
        const startTime = log.startTime
          ? new Date(log.startTime).toLocaleTimeString([], {
              hour: "2-digit",
              minute: "2-digit",
            })
          : "N/A";
        const endTime = log.endTime
          ? new Date(log.endTime).toLocaleTimeString([], {
              hour: "2-digit",
              minute: "2-digit",
            })
          : "N/A";
        const duration = log.duration || "N/A";
        const html = `
          <div class="schedule-item">
            <div><strong>${
              action === "NPK Injection" ? "Cycle " + npkCycle++ : "Cycle " + pumpCycle++
            }</strong> - ${action}</div>
            <div>Start: ${startTime}</div>
            <div>End: ${endTime}</div>
            <div>Duration: ${duration}</div>
          </div>
          <hr>
        `;

        if (action === "NPK Injection" && npkDiv) npkDiv.innerHTML += html;
        else if (pumpDiv) pumpDiv.innerHTML += html;
      });
    })
    .catch((err) => {
      logEvent("Firebase read error (Manual Logs): " + err.message, "bad");
    });
}

// Mode selector for scheduling view
if (modeSelect) {
  modeSelect.addEventListener("change", () => {
    const mode = modeSelect.value;
    const modeLabel = document.getElementById("mode-label");
    if (modeLabel) modeLabel.textContent = mode === "automatic" ? "Auto" : "Manual";

    if (mode === "automatic") {
      document.querySelectorAll(".pump-actions button, .dosing-actions button").forEach(btn => {
        if (btn) btn.disabled = true;
      });
      const logsRef = database.ref("logs");
      logsRef.once("value", snapshot => {
        const data = snapshot.val();
        if (data) displayAllLogs(data);
      });
    } else {
      document.querySelectorAll(".pump-actions button, .dosing-actions button").forEach(btn => {
        if (btn) btn.disabled = false;
      });
      if (npkScheduleDetails) npkScheduleDetails.innerHTML = "";
      if (pumpScheduleDetails) pumpScheduleDetails.innerHTML = "";
    }
  });
  modeSelect.dispatchEvent(new Event("change"));
}

// ==================== NEW: ANALYTICS VIEW LOGIC ====================

let npkChart;

function drawNPKChart() {
  const ctx = document.getElementById('npkChart')?.getContext('2d');
  if (!ctx) return;
  
  if (npkChart) npkChart.destroy();

  const logsRef = database.ref("logs");

  logsRef.once("value", snapshot => {
    const data = snapshot.val();
    if (!data) return;

    const dailyData = {};
    Object.values(data).forEach(log => {
      if (log.action === "NPK Injection") {
        const date = log.date || "Unknown";
        if (!dailyData[date]) {
          dailyData[date] = { 
            N: { manual: 0, auto: 0, count: 0 }, 
            P: { manual: 0, auto: 0, count: 0 }, 
            K: { manual: 0, auto: 0, count: 0 } 
          };
        }

        const mode = log.mode || "Automatic";
        const isManual = mode === "Manual";
        
        if (isManual) {
          dailyData[date].N.manual += log.N || 0;
          dailyData[date].P.manual += log.P || 0;
          dailyData[date].K.manual += log.K || 0;
        } else {
          dailyData[date].N.auto += log.N || 0;
          dailyData[date].P.auto += log.P || 0;
          dailyData[date].K.auto += log.K || 0;
        }
        
        dailyData[date].N.count++;
        dailyData[date].P.count++;
        dailyData[date].K.count++;
      }
    });

    const labels = [];
    const NManualData = [];
    const NAutoData = [];
    const PManualData = [];
    const PAutoData = [];
    const KManualData = [];
    const KAutoData = [];

    Object.keys(dailyData).sort().forEach(date => {
      const day = dailyData[date];
      labels.push(date);
      
      NManualData.push(day.N.manual);
      NAutoData.push(day.N.auto);
      PManualData.push(day.P.manual);
      PAutoData.push(day.P.auto);
      KManualData.push(day.K.manual);
      KAutoData.push(day.K.auto);
    });

    npkChart = new Chart(ctx, {
      type: 'bar',
      data: {
        labels: labels,
        datasets: [
          { 
            label: 'N (Manual)', 
            data: NManualData, 
            backgroundColor: 'rgba(255,99,132,0.6)',
            borderColor: 'rgba(255,99,132,1)',
            borderWidth: 1
          },
          { 
            label: 'N (Auto)', 
            data: NAutoData, 
            backgroundColor: 'rgba(255,99,132,0.3)',
            borderColor: 'rgba(255,99,132,1)',
            borderWidth: 1,
            borderDash: [5, 5]
          },
          { 
            label: 'P (Manual)', 
            data: PManualData, 
            backgroundColor: 'rgba(54,162,235,0.6)',
            borderColor: 'rgba(54,162,235,1)',
            borderWidth: 1
          },
          { 
            label: 'P (Auto)', 
            data: PAutoData, 
            backgroundColor: 'rgba(54,162,235,0.3)',
            borderColor: 'rgba(54,162,235,1)',
            borderWidth: 1,
            borderDash: [5, 5]
          },
          { 
            label: 'K (Manual)', 
            data: KManualData, 
            backgroundColor: 'rgba(75,192,192,0.6)',
            borderColor: 'rgba(75,192,192,1)',
            borderWidth: 1
          },
          { 
            label: 'K (Auto)', 
            data: KAutoData, 
            backgroundColor: 'rgba(75,192,192,0.3)',
            borderColor: 'rgba(75,192,192,1)',
            borderWidth: 1,
            borderDash: [5, 5]
          }
        ]
      },
      options: {
        responsive: true,
        title: { 
          display: true, 
          text: 'NPK Nutrient Injections',
          fontSize: 16
        },
        scales: {
          xAxes: [{
            stacked: false,
            scaleLabel: {
              display: true,
              labelString: 'Date'
            }
          }],
          yAxes: [{
            stacked: false,
            ticks: {
              beginAtZero: true
            },
            scaleLabel: {
              display: true,
              labelString: 'PPM Injected'
            }
          }]
        },
        legend: {
          display: true,
          position: 'bottom'
        },
        tooltips: {
          mode: 'index',
          intersect: false,
          callbacks: {
            label: function(tooltipItem, data) {
              const label = data.datasets[tooltipItem.datasetIndex].label || '';
              const value = tooltipItem.yLabel;
              return label + ': ' + value.toFixed(1) + ' PPM';
            }
          }
        }
      }
    });
  });
}

// ==================== UI EVENT HANDLERS ====================

if (splash) {
  splash.addEventListener("click", () => {
    splash.classList.add("hidden");
    app.classList.add("visible");
    render();
  });
}

navButtons.forEach((btn) => {
  btn.addEventListener("click", (e) => {
    handleNavigation(e.currentTarget.dataset.view);
  });
});

function handleNavigation(viewId) {
  const targetViewId = viewId + "-view";
  document.querySelectorAll(".view").forEach((view) => {
    view.classList.remove("active-view");
  });
  const targetView = document.getElementById(targetViewId);
  if (targetView) {
    targetView.classList.add("active-view");
  }
  navButtons.forEach((btn) => {
    btn.classList.remove("active");
    if (btn.getAttribute("data-view") === viewId) {
      btn.classList.add("active");
    }
  });
  if (app) app.classList.remove("sidebar-open");
  
  // Draw chart when analytics view is opened
  if (targetViewId === 'analytics-view') {
    drawNPKChart();
  }
}

if (mobileMenuBtn) {
  mobileMenuBtn.addEventListener("click", () => {
    if (app) app.classList.toggle("sidebar-open");
  });
}

if (sidebarToggleBtn) {
  sidebarToggleBtn.addEventListener("click", () => {
    if (app) app.classList.toggle("sidebar-minimized");
    const icon = sidebarToggleBtn.querySelector("i");
    const isMinimized = app.classList.contains("sidebar-minimized");

    if (icon) {
      if (isMinimized) {
        icon.classList.remove("fa-grip-lines-vertical");
        icon.classList.add("fa-angles-right");
        const span = sidebarToggleBtn.querySelector("span");
        if (span) span.textContent = "Expand";
      } else {
        icon.classList.remove("fa-angles-right");
        icon.classList.add("fa-grip-lines-vertical");
        const span = sidebarToggleBtn.querySelector("span");
        if (span) span.textContent = "Minimize";
      }
    }
  });
}

if (systemPowerToggle) {
  systemPowerToggle.addEventListener("change", (e) => {
    state.systemOn = e.target.checked;
    const status = state.systemOn ? "ON" : "OFF";
    logEvent(`System Power turned ${status}.`, state.systemOn ? "ok" : "bad");

    database
      .ref("controls/systemPower")
      .set(status)
      .then(() => console.log(`System Power is now ${status} in Firebase`))
      .catch((err) => console.error("Firebase write error:", err));

    render();
  });
}

if (modeToggle) {
  modeToggle.addEventListener("change", (e) => {
    state.modeAuto = e.target.checked;
    const mode = state.modeAuto ? "Auto" : "Manual";
    logEvent(`System switched to ${mode} mode.`, "warn");

    database
      .ref("controls/modeAuto")
      .set(mode)
      .then(() => console.log(`Mode is now ${mode} in Firebase`))
      .catch((err) => console.error("Firebase write error:", err));

    render();

    if (!state.modeAuto) displayManualLogs();
    else renderSchedule();
  });
}

if (cropSelect) {
  cropSelect.addEventListener("change", (e) => {
    state.crop = e.target.value;
    logEvent(`Crop changed to ${state.crop.toUpperCase()}.`, "warn");

    database
      .ref("controls/crop")
      .set(state.crop)
      .then(() => {
        console.log(`Crop updated to ${state.crop} in Firebase`);
        logEvent(`ESP32 will use ${state.crop.toUpperCase()} target values.`, "ok");
      })
      .catch((err) => {
        console.error("Firebase crop update error:", err);
        logEvent("Failed to update crop selection.", "bad");
      });
      
    render();
  });
}

if (pumpOnBtn) {
  pumpOnBtn.addEventListener("click", () => handlePumpControl(true));
}

if (pumpOffBtn) {
  pumpOffBtn.addEventListener("click", () => handlePumpControl(false));
}

// NEW: Event listeners for manual injection buttons
if (injectNBtn) {
  injectNBtn.addEventListener("click", () => {
    const amount = nAmountInput.value;
    handleManualNutrientInjection("N", amount);
  });
}

if (injectPBtn) {
  injectPBtn.addEventListener("click", () => {
    const amount = pAmountInput.value;
    handleManualNutrientInjection("P", amount);
  });
}

if (injectKBtn) {
  injectKBtn.addEventListener("click", () => {
    const amount = kAmountInput.value;
    handleManualNutrientInjection("K", amount);
  });
}

if (injectAllBtn) {
  injectAllBtn.addEventListener("click", () => {
    handleInjectAll();
  });
}

// NEW: Allow Enter key to trigger injection
if (nAmountInput) {
  nAmountInput.addEventListener("keypress", (e) => {
    if (e.key === "Enter" && !injectNBtn.disabled) {
      injectNBtn.click();
    }
  });
}

if (pAmountInput) {
  pAmountInput.addEventListener("keypress", (e) => {
    if (e.key === "Enter" && !injectPBtn.disabled) {
      injectPBtn.click();
    }
  });
}

if (kAmountInput) {
  kAmountInput.addEventListener("keypress", (e) => {
    if (e.key === "Enter" && !injectKBtn.disabled) {
      injectKBtn.click();
    }
  });
}

// ==================== INITIALIZATION ====================

document.addEventListener('DOMContentLoaded', () => {
    // 1. Hide all views
    document.querySelectorAll(".view").forEach((view) => {
        // Remove active class (which sets display:flex)
        view.classList.remove("active-view");
  });

    // 2. Explicitly show only the initial active view (Dashboard)
    const initialView = document.getElementById("dashboard-view");
    if (initialView) {
      // Add active class (which sets display:flex)
      initialView.classList.add("active-view");
    }
});

nitrogenChart = initializeChart("nitrogen", "var(--leaf)");
phosphorusChart = initializeChart("phosphorus", "var(--amber)");
potassiumChart = initializeChart("potassium", "var(--red)");

if (cropSelect) cropSelect.value = state.crop;
if (systemPowerToggle) systemPowerToggle.checked = state.systemOn;
if (modeToggle) modeToggle.checked = state.modeAuto;

const initialModeLabel = document.getElementById("mode-label");
if (initialModeLabel) initialModeLabel.textContent = state.modeAuto ? "Auto" : "Manual";

setInterval(simulationTick, 2000);

logEvent("Modular Hydroponic System Initializing...", "ok");
logEvent("Waiting for Firebase authentication...", "ok");
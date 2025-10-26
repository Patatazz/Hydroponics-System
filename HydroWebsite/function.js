// --- 1️⃣ Firebase Initialization (Add this at the top of function.js) ---
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

firebase.initializeApp(firebaseConfig);
const database = firebase.database();
const auth = firebase.auth(); // Add auth instance

// === AUTHENTICATION: Sign in anonymously or with your ESP32 user ===
// Option 1: Anonymous sign-in (easiest for web app)
// auth.signInAnonymously()
//   .then(() => {
//     console.log("✓ Signed in to Firebase anonymously");
//     startFirebaseListeners(); // Start listeners after authentication
//   })
//   .catch((error) => {
//     console.error("Firebase Auth Error:", error);
//     logEvent("Failed to authenticate with Firebase", "bad");
//   });

// Option 2: Sign in with the same email/password as ESP32 (uncomment if preferred)

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

const crops = {
  lettuce: { N: 150, P: 50, K: 200, water: 70 },
  bokchoy: { N: 180, P: 45, K: 220, water: 65 },
};

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
      { start: "18:00", duration: 20, active: false },
    ],
  },
  npkData: { N: 0, P: 0, K: 0 },
};

let nitrogenChart, phosphorusChart, potassiumChart;

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
const doseNBtn = document.getElementById("dose-N-btn");
const dosePBtn = document.getElementById("dose-P-btn");
const doseKBtn = document.getElementById("dose-K-btn");

const dashboardView = document.getElementById("dashboard-view");
const schedulingView = document.getElementById("scheduling-view");
const analyticsView = document.getElementById("analytics-view");
const notificationsView = document.getElementById("notifications-view");
const logList = document.getElementById("log-list");
const npkScheduleDetails = document.getElementById("npk-schedule-details");
const pumpScheduleDetails = document.getElementById("pump-schedule-details");

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

  valueEl.textContent = `${Number(value).toFixed(1)} ppm`;
  targetEl.textContent = `${target} ppm`;

  const diff = Math.abs(value - target);
  const tolerance = target * 0.15;
  statusEl.className = "status-dot";

  let color;
  if (value < 10) {
    statusEl.classList.add("bad");
    color = "var(--red)";
  } else if (diff > tolerance) {
    statusEl.classList.add("warn-dot");
    color = "var(--amber)";
  } else {
    statusEl.classList.add("ok");
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
  tankFill.style.height = `${Number(level).toFixed(0)}%`;
  waterValueEl.textContent = `${Number(level).toFixed(0)}%`;
  waterTargetEl.textContent = `${target}%`;

  if (waterRawValueEl) {
    waterRawValueEl.textContent = `${Number(level).toFixed(1)}%`;
  }

  waterStatusEl.className = "status-dot";
  if (level < target - 15) {
    waterStatusEl.classList.add("bad");
    waterStatusEl.textContent = "LOW";
  } else if (level < target - 5) {
    waterStatusEl.classList.add("warn-dot");
    waterStatusEl.textContent = "ATTENTION";
  } else {
    waterStatusEl.classList.add("ok");
    waterStatusEl.textContent = "OK";
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
  npkScheduleDetails.innerHTML = `
              <div class="schedule-item">
                  <h5>Daily Injection</h5>
                  <p><strong>Time:</strong> ${npkTime}</p>
                  <p><strong>Next Run:</strong> ${
                    state.schedule.npkInjection.executedToday
                      ? "Tomorrow"
                      : "Today"
                  }</p>
                  <p><strong>Status:</strong> ${
                    state.schedule.npkInjection.executedToday
                      ? "Executed"
                      : "Pending"
                  }</p>
              </div>
          `;

  pumpScheduleDetails.innerHTML = state.schedule.pumpCycles
    .map((cycle, index) => {
      return `
                  <div class="schedule-item">
                      <h5>Pump Cycle ${index + 1}</h5>
                      <p><strong>Start Time:</strong> ${cycle.start}</p>
                      <p><strong>Duration:</strong> ${
                        cycle.duration
                      } minutes</p>
                      <p><strong>Status:</strong> ${
                        cycle.active ? "Running" : "Scheduled"
                      }</p>
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

  systemStatusText.textContent = state.systemOn ? "System ON" : "System OFF";

  if (modeLabel) modeLabel.textContent = state.modeAuto ? "Auto" : "Manual";

  const isDisabled = !state.systemOn || state.modeAuto;
  pumpOnBtn.disabled = isDisabled;
  pumpOffBtn.disabled = isDisabled;
  doseNBtn.disabled = isDisabled;  
  dosePBtn.disabled = isDisabled; 
  doseKBtn.disabled = isDisabled; 

  logList.innerHTML = state.logs
    .map((log) => `<li class="${log.type}">[${log.time}] ${log.message}</li>`)
    .join("");

  renderSchedule();

  if (!isDisabled) {
    pumpOnBtn.classList.toggle("primary", state.actuators.pump);
    pumpOffBtn.classList.toggle("warn", !state.actuators.pump);
  } else {
    pumpOnBtn.classList.remove("primary");
    pumpOffBtn.classList.remove("warn");
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
          `${key} LEVEL CRITICAL: Value dropped to ${Number(value).toFixed(
            1
          )} ppm.`,
          "bad"
        );
      }
      alerts++;
    } else if (diff > threshold) {
      const status = value > t ? "HIGH" : "LOW";
      if (Math.random() < 0.02) {
        logEvent(
          `${key} LEVEL ${status}: Value is ${Number(value).toFixed(
            1
          )} ppm (Target: ${t} ppm).`,
          "warn"
        );
      }
      alerts++;
    }
  });

  if (state.sensors.water_level < target.water - 10) {
    if (Math.random() < 0.05) {
      logEvent(
        `WATER LEVEL LOW: Current level ${Number(
          state.sensors.water_level
        ).toFixed(1)}% (Target: ${target.water}%).`,
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
    database.ref("controls/autoDoseTrigger").set(Date.now());
    logEvent("AUTOMATED: Daily NPK injection triggered.", "ok");
    npkSchedule.executedToday = true;
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

// UPDATED: Now called after authentication succeeds
function startFirebaseListeners() {
  database.ref("sensors").on(
    "value",
    (snapshot) => {
      const data = snapshot.val();
      if (data) {
        // Match the ESP32 field names: nitrogen_ppm, phosphorus_ppm, potassium_ppm
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

  database.ref("actuators/pump").on(
    "value", 
    (snapshot) => {
      const val = snapshot.val();
      const isPumpOn = val === 1 || val === true;
      state.actuators.pump = isPumpOn;
      render();
  });

  // Listen for dose flag changes to re-enable buttons
  database.ref("controls/dose").on("value", (snapshot) => {
    const doseStates = snapshot.val();
    
    // Handle case when doseStates is null (no data yet)
    const nDosing = doseStates ? (doseStates.N === true) : false;
    const pDosing = doseStates ? (doseStates.P === true) : false;
    const kDosing = doseStates ? (doseStates.K === true) : false;
    
    const anyDosing = nDosing || pDosing || kDosing;
    
    // Update button states
    updateDoseButtonStates(nDosing, pDosing, kDosing, anyDosing);
  });
}



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

function handleNPKDose(nutrient) {
  if (!state.systemOn) {
    logEvent(`Manual ${nutrient} dosing denied: System is OFF.`, "bad");
    return;
  }

  if (state.modeAuto) {
    logEvent(
      `Manual ${nutrient} dosing denied: System is in Auto mode.`,
      "warn"
    );
    return;
  }

  // Check if any dosing is already in progress
  database.ref("controls/dose").once("value").then((snapshot) => {
    const doseStates = snapshot.val();
    if (doseStates && (doseStates.N || doseStates.P || doseStates.K)) {
      logEvent(
        `Cannot start ${nutrient} dosing: Another nutrient is currently being dosed.`,
        "warn"
      );
      return;
    }

    // IMMEDIATELY disable ALL buttons to prevent double clicks
    doseNBtn.disabled = true;
    dosePBtn.disabled = true;
    doseKBtn.disabled = true;

    // Update the clicked button appearance
    const button = document.getElementById(`dose-${nutrient}-btn`);
    if (button) {
      button.classList.add("active-inject");
      button.textContent = `Dosing ${nutrient}...`;
    }

    // Only send nutrient flag - ESP32 handles target values
    database
      .ref(`controls/dose/${nutrient}`)
      .set(true)
      .then(() => {
        logEvent(
          `Manual Control: ${nutrient} dosing started (Command sent to ESP32).`,
          "ok"
        );
      })
      .catch((error) => {
        logEvent(
          `Failed to write ${nutrient} dose command to Firebase: ${error.message}`,
          "bad"
        );
        
        // Re-enable buttons on error
        const canDose = state.systemOn && !state.modeAuto;
        if (canDose) {
          doseNBtn.disabled = false;
          dosePBtn.disabled = false;
          doseKBtn.disabled = false;
        }
        
        if (button) {
          button.classList.remove("active-inject");
          button.textContent = `Inject ${nutrient}`;
        }
      });
  });
}

function updateDoseButtonStates(nDosing, pDosing, kDosing, anyDosing) {
  const isManualMode = !state.modeAuto;
  const systemIsOn = state.systemOn;
  
  // Base condition: system must be ON and in Manual mode
  const canDose = systemIsOn && isManualMode;
  
  // If ANY nutrient is dosing, disable ALL buttons
  if (anyDosing) {
    // Nitrogen button
    if (nDosing) {
      doseNBtn.disabled = true;
      doseNBtn.textContent = "Dosing N...";
      doseNBtn.classList.add("active-inject");
    } else {
      doseNBtn.disabled = true;  // Disabled because another is dosing
      doseNBtn.textContent = "Inject N";
      doseNBtn.classList.remove("active-inject");
    }
    
    // Phosphorus button
    if (pDosing) {
      dosePBtn.disabled = true;
      dosePBtn.textContent = "Dosing P...";
      dosePBtn.classList.add("active-inject");
    } else {
      dosePBtn.disabled = true;  // Disabled because another is dosing
      dosePBtn.textContent = "Inject P";
      dosePBtn.classList.remove("active-inject");
    }
    
    // Potassium button
    if (kDosing) {
      doseKBtn.disabled = true;
      doseKBtn.textContent = "Dosing K...";
      doseKBtn.classList.add("active-inject");
    } else {
      doseKBtn.disabled = true;  // Disabled because another is dosing
      doseKBtn.textContent = "Inject K";
      doseKBtn.classList.remove("active-inject");
    }
  } else {
    // No dosing in progress - enable/disable based on system state
    doseNBtn.disabled = !canDose;
    doseNBtn.textContent = "Inject N";
    doseNBtn.classList.remove("active-inject");
    
    dosePBtn.disabled = !canDose;
    dosePBtn.textContent = "Inject P";
    dosePBtn.classList.remove("active-inject");
    
    doseKBtn.disabled = !canDose;
    doseKBtn.textContent = "Inject K";
    doseKBtn.classList.remove("active-inject");
  }
}


splash.addEventListener("click", () => {
  splash.classList.add("hidden");
  app.classList.add("visible");
  render();
});

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
  app.classList.remove("sidebar-open");
}

mobileMenuBtn.addEventListener("click", () => {
  app.classList.toggle("sidebar-open");
});

sidebarToggleBtn.addEventListener("click", () => {
  app.classList.toggle("sidebar-minimized");
  const icon = sidebarToggleBtn.querySelector("i");
  const isMinimized = app.classList.contains("sidebar-minimized");

  if (isMinimized) {
    icon.classList.remove("fa-grip-lines-vertical");
    icon.classList.add("fa-angles-right");
    sidebarToggleBtn.querySelector("span").textContent = "Expand";
  } else {
    icon.classList.remove("fa-angles-right");
    icon.classList.add("fa-grip-lines-vertical");
    sidebarToggleBtn.querySelector("span").textContent = "Minimize";
  }
});

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
});

cropSelect.addEventListener("change", (e) => {
  state.crop = e.target.value;
  logEvent(`Crop changed to ${state.crop.toUpperCase()}.`, "warn");

  // Send crop change to Firebase so ESP32 knows which target values to use
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

pumpOnBtn.addEventListener("click", () => handlePumpControl(true));
pumpOffBtn.addEventListener("click", () => handlePumpControl(false));

doseNBtn.addEventListener("click", () => handleNPKDose("N"));
dosePBtn.addEventListener("click", () => handleNPKDose("P"));
doseKBtn.addEventListener("click", () => handleNPKDose("K"));

nitrogenChart = initializeChart("nitrogen", "var(--leaf)");
phosphorusChart = initializeChart("phosphorus", "var(--amber)");
potassiumChart = initializeChart("potassium", "var(--red)");

cropSelect.value = state.crop;
systemPowerToggle.checked = state.systemOn;
modeToggle.checked = state.modeAuto;

const initialModeLabel = document.getElementById("mode-label");
if (initialModeLabel)
  initialModeLabel.textContent = state.modeAuto ? "Auto" : "Manual";

// Don't call startFirebaseListeners() here anymore - it's called after auth
setInterval(simulationTick, 2000);

logEvent("Modular Hydroponic System Initializing...", "ok");
logEvent("Waiting for Firebase authentication...", "ok");

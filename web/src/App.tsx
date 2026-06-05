import { useCallback, useContext, useEffect, useMemo, useState } from "react";
import "./App.css";
import { connect as gattConnect } from "@zmkfirmware/zmk-studio-ts-client/transport/gatt";
import { connect as serialConnect } from "@zmkfirmware/zmk-studio-ts-client/transport/serial";
import {
  ZMKAppContext,
  ZMKConnection,
  ZMKCustomSubsystem,
} from "@cormoran/zmk-studio-react-hook";
import {
  Notification as InputModuleNotification,
  Request,
  Response,
} from "./proto/dya/input_module/input_module";
import type {
  ModuleCapabilities,
  ModuleProfile,
  ModuleState,
  Request as InputModuleRequest,
} from "./proto/dya/input_module/input_module";

export const SUBSYSTEM_IDENTIFIER = "dya__input_module";

const SUBSYSTEM_CANDIDATES = [
  SUBSYSTEM_IDENTIFIER,
  "zmk__input_module",
  "input_module",
];

type StateMap = Record<number, ModuleState>;

const demoStates = createDemoStates();

function App() {
  const [demoMode, setDemoMode] = useState(false);

  return (
    <div className="app-shell">
      <header className="hero">
        <div>
          <p className="eyebrow">DYA Studio SubSystem</p>
          <h1>Input Module Compass</h1>
          <p className="lead">
            Runtime visibility and next-boot module selection for ZMK split
            input devices.
          </p>
        </div>
        <button className="ghost" onClick={() => setDemoMode((value) => !value)}>
          {demoMode ? "Disable Demo" : "Enable Demo"}
        </button>
      </header>

      <ZMKConnection
        renderDisconnected={({ connect, isLoading, error }) => (
          <>
            <section className="panel connection-panel">
              <div>
                <p className="eyebrow">Connection</p>
                <h2>Connect to ZMK Studio</h2>
                <p className="muted">
                  Bluetooth uses Web Bluetooth. Serial uses Web Serial.
                </p>
              </div>
              <div className="actions">
                <button
                  className="primary"
                  disabled={isLoading}
                  onClick={() => connect(gattConnect)}
                >
                  Connect Bluetooth
                </button>
                <button
                  className="secondary"
                  disabled={isLoading}
                  onClick={() => connect(serialConnect)}
                >
                  Connect Serial
                </button>
              </div>
              {isLoading && <p className="status">Connecting...</p>}
              {error && <p className="error">{error}</p>}
            </section>
            {demoMode && <InputModuleStudio demoMode />}
          </>
        )}
        renderConnected={({ disconnect, deviceName }) => (
          <>
            <section className="panel connection-panel connected">
              <div>
                <p className="eyebrow">Connected</p>
                <h2>{deviceName}</h2>
              </div>
              <button className="secondary" onClick={disconnect}>
                Disconnect
              </button>
            </section>
            <InputModuleStudio demoMode={demoMode} />
          </>
        )}
      />
    </div>
  );
}

function InputModuleStudio({ demoMode = false }: { demoMode?: boolean }) {
  const zmkApp = useContext(ZMKAppContext);
  const [states, setStates] = useState<StateMap>(demoMode ? demoStates : {});
  const [selectedTarget, setSelectedTarget] = useState(0);
  const [selectedProfile, setSelectedProfile] = useState<number | null>(null);
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState(demoMode ? "Demo state loaded" : "");

  const subsystem = useMemo(() => {
    if (!zmkApp || demoMode) {
      return null;
    }

    for (const candidate of SUBSYSTEM_CANDIDATES) {
      const found = zmkApp.findSubsystem(candidate);
      if (found) {
        return found;
      }
    }

    return null;
  }, [demoMode, zmkApp]);

  const canUseSubsystem = demoMode || Boolean(subsystem);
  const targetState = states[selectedTarget];
  const fallbackState = states[0];
  const visibleState = targetState ?? fallbackState;
  const targetKnown = selectedTarget === 0 || Boolean(targetState);
  const targetProfiles = visibleState?.profiles ?? [];
  const selectedProfileInfo =
    selectedProfile == null
      ? null
      : targetProfiles.find((profile) => profile.id === selectedProfile) ?? null;
  const canSave =
    canUseSubsystem &&
    targetKnown &&
    selectedProfileInfo != null &&
    selectedProfileInfo.available &&
    !busy;

  const targetOptions = useMemo(() => {
    const sources = new Set<number>([0, 1]);
    for (const source of Object.keys(states)) {
      sources.add(Number(source));
    }

    return Array.from(sources)
      .sort((a, b) => a - b)
      .map((source) => ({
        source,
        label: sourceLabel(source),
        known: Boolean(states[source]),
      }));
  }, [states]);

  const availableSubsystems = zmkApp?.state.customSubsystems?.subsystems ?? [];

  const upsertState = useCallback((state: ModuleState) => {
    setStates((current) => ({
      ...current,
      [state.source]: state,
    }));
  }, []);

  const callRPC = useCallback(
    async (request: InputModuleRequest): Promise<Response> => {
      if (!zmkApp?.state.connection || !subsystem) {
        throw new Error("dya__input_module subsystem is not available");
      }

      const service = new ZMKCustomSubsystem(
        zmkApp.state.connection,
        subsystem.index,
      );
      const payload = Request.encode(Request.create(request)).finish();
      const responsePayload = await service.callRPC(payload);

      if (!responsePayload) {
        throw new Error("Empty RPC response");
      }

      const response = Response.decode(responsePayload);
      if (response.error) {
        throw new Error(response.error.message || "RPC error");
      }

      return response;
    },
    [subsystem, zmkApp?.state.connection],
  );

  const loadLocal = useCallback(async () => {
    if (demoMode) {
      setStates(demoStates);
      setStatus("Demo state loaded");
      return;
    }

    setBusy(true);
    setStatus("");
    try {
      const response = await callRPC({ getState: {} });
      const state = response.getState?.state;
      if (!state) {
        throw new Error("GetState response did not include module state");
      }
      upsertState(state);
      setStatus("Loaded central module state");
    } catch (error) {
      setStatus(error instanceof Error ? error.message : "Failed to load state");
    } finally {
      setBusy(false);
    }
  }, [callRPC, demoMode, upsertState]);

  const loadAll = useCallback(async () => {
    if (demoMode) {
      setStates(demoStates);
      setStatus("Demo split state loaded");
      return;
    }

    setBusy(true);
    setStatus("");
    try {
      await callRPC({ getAllStates: {} });
      const response = await callRPC({ getState: {} });
      const state = response.getState?.state;
      if (state) {
        upsertState(state);
      }
      setStatus("Requested split module states");
    } catch (error) {
      setStatus(
        error instanceof Error ? error.message : "Failed to request split states",
      );
    } finally {
      setBusy(false);
    }
  }, [callRPC, demoMode, upsertState]);

  const saveSelected = useCallback(async () => {
    if (selectedProfile == null) {
      return;
    }

    if (demoMode) {
      setStates((current) => ({
        ...current,
        [selectedTarget]: markSelected(current[selectedTarget], selectedProfile),
      }));
      setStatus(
        `${sourceLabel(selectedTarget)} will use ${profileLabel(selectedProfileInfo)} on next boot`,
      );
      return;
    }

    setBusy(true);
    setStatus("");
    try {
      const response = await callRPC({
        setSelected: {
          target: selectedTarget,
          profileId: selectedProfile,
        },
      });
      const state = response.setSelected?.state;
      if (state && selectedTarget === 0) {
        upsertState(state);
      }
      setStatus(
        `Saved ${profileLabel(selectedProfileInfo)} for ${sourceLabel(selectedTarget)}. Reboot is required for device init changes.`,
      );
    } catch (error) {
      setStatus(
        error instanceof Error ? error.message : "Failed to save selection",
      );
    } finally {
      setBusy(false);
    }
  }, [
    callRPC,
    demoMode,
    selectedProfile,
    selectedProfileInfo,
    selectedTarget,
    upsertState,
  ]);

  useEffect(() => {
    if (demoMode) {
      setStates(demoStates);
      setStatus("Demo state loaded");
    } else {
      setStates({});
      setStatus("");
    }
  }, [demoMode]);

  useEffect(() => {
    const state = states[selectedTarget];
    if (state) {
      setSelectedProfile(state.selectedProfileId);
    } else if (states[0]) {
      setSelectedProfile(states[0].selectedProfileId);
    }
  }, [selectedTarget, states]);

  useEffect(() => {
    if (!demoMode && subsystem) {
      void loadAll();
    }
  }, [demoMode, loadAll, subsystem]);

  useEffect(() => {
    if (!zmkApp || !subsystem || demoMode) {
      return;
    }

    return zmkApp.onNotification({
      type: "custom",
      subsystemIndex: subsystem.index,
      callback: (notification) => {
        try {
          const decoded = InputModuleNotification.decode(notification.payload);
          const state = decoded.state?.state;
          if (state) {
            upsertState(state);
            setStatus(
              `Received ${sourceLabel(state.source)} state report: ${statusLabel(state.status)}`,
            );
          }
        } catch (error) {
          setStatus(
            error instanceof Error
              ? `Failed to decode notification: ${error.message}`
              : "Failed to decode notification",
          );
        }
      },
    });
  }, [demoMode, subsystem, upsertState, zmkApp]);

  return (
    <main className="studio-grid">
      <section className="panel subsystem-panel">
        <div className="panel-head">
          <div>
            <p className="eyebrow">Subsystem</p>
            <h2>Module Selection RPC</h2>
          </div>
          <span className={canUseSubsystem ? "badge ok" : "badge warn"}>
            {demoMode
              ? "demo"
              : subsystem
                ? `${subsystem.identifier}#${subsystem.index}`
                : "not found"}
          </span>
        </div>

        {!demoMode && zmkApp?.state.connection && !subsystem && (
          <p className="warning">
            dya__input_module is not advertised. Available subsystems:{" "}
            {availableSubsystems
              .map((item) => `${item.identifier}#${item.index}`)
              .join(", ") || "none"}
          </p>
        )}

        <div className="actions">
          <button
            className="primary"
            disabled={busy || !canUseSubsystem}
            onClick={loadAll}
          >
            Refresh Split
          </button>
          <button
            className="secondary"
            disabled={busy || !canUseSubsystem}
            onClick={loadLocal}
          >
            Refresh Local
          </button>
        </div>

        {status && <p className="status">{status}</p>}
      </section>

      <section className="panel target-panel">
        <div className="panel-head">
          <div>
            <p className="eyebrow">Target</p>
            <h2>Choose Side and Profile</h2>
          </div>
          <select
            value={selectedTarget}
            onChange={(event) => setSelectedTarget(Number(event.target.value))}
          >
            {targetOptions.map((target) => (
              <option key={target.source} value={target.source}>
                {target.label}
                {target.known ? "" : " (unknown)"}
              </option>
            ))}
          </select>
        </div>

        {!targetKnown && (
          <p className="warning">
            Peripheral candidates are unknown. Run Refresh Split before saving a
            peripheral selection.
          </p>
        )}

        <div className="profile-grid">
          {targetProfiles.map((profile) => (
            <button
              key={profile.id}
              className={[
                "profile-card",
                profile.id === selectedProfile ? "selected" : "",
                profile.applied ? "applied" : "",
                profile.available ? "" : "unavailable",
              ]
                .filter(Boolean)
                .join(" ")}
              disabled={!targetKnown || !profile.available}
              onClick={() => setSelectedProfile(profile.id)}
            >
              <span className="profile-name">{profile.name}</span>
              <span className="profile-id">ID {profile.id}</span>
              <span className="chip-row">
                {capabilityLabels(profile.capabilities).map((label) => (
                  <span className="chip" key={label}>
                    {label}
                  </span>
                ))}
              </span>
              <span className="profile-flags">
                {profile.selected && "selected "}
                {profile.applied && "applied "}
                {!profile.available && "not available on this side"}
              </span>
            </button>
          ))}
        </div>

        <button className="commit" disabled={!canSave} onClick={saveSelected}>
          Save for Next Boot
        </button>
      </section>

      <section className="state-stack">
        {Object.values(states).length === 0 ? (
          <article className="panel empty-state">
            <p className="eyebrow">State</p>
            <h2>No module state loaded</h2>
            <p className="muted">
              Connect to the central side and refresh split state.
            </p>
          </article>
        ) : (
          Object.values(states)
            .sort((a, b) => a.source - b.source)
            .map((state) => <StateCard key={state.source} state={state} />)
        )}
      </section>
    </main>
  );
}

function StateCard({ state }: { state: ModuleState }) {
  const available = state.profiles.filter((profile) => profile.available).length;

  return (
    <article className="panel state-card">
      <div className="panel-head">
        <div>
          <p className="eyebrow">{sourceLabel(state.source)}</p>
          <h2>{state.selectedProfileName || `Profile ${state.selectedProfileId}`}</h2>
        </div>
        <span className={state.rebootRequired ? "badge warn" : "badge ok"}>
          {state.rebootRequired ? "reboot needed" : "applied"}
        </span>
      </div>

      <dl className="state-facts">
        <div>
          <dt>Selected</dt>
          <dd>
            {state.selectedProfileName} #{state.selectedProfileId}
          </dd>
        </div>
        <div>
          <dt>Applied</dt>
          <dd>
            {state.appliedProfileName} #{state.appliedProfileId}
          </dd>
        </div>
        <div>
          <dt>Status</dt>
          <dd>{statusLabel(state.status)}</dd>
        </div>
        <div>
          <dt>Candidates</dt>
          <dd>
            {available}/{state.profiles.length} available
          </dd>
        </div>
      </dl>
    </article>
  );
}

function sourceLabel(source: number) {
  if (source === 0) {
    return "Central / Local";
  }

  return `Peripheral ${source}`;
}

function profileLabel(profile: ModuleProfile | null) {
  return profile ? `${profile.name} (#${profile.id})` : "selected profile";
}

function statusLabel(status: number) {
  return status === 0 ? "ok" : `error ${status}`;
}

function capabilityLabels(capabilities: ModuleCapabilities | undefined) {
  if (!capabilities) {
    return ["NONE"];
  }

  const labels = [
    capabilities.kscan ? "GPIO" : "",
    capabilities.encoder ? "ENC" : "",
    capabilities.adc ? "ADC" : "",
    capabilities.spi ? "SPI" : "",
    capabilities.i2c ? "I2C" : "",
  ].filter(Boolean);

  return labels.length > 0 ? labels : ["NONE"];
}

function markSelected(state: ModuleState | undefined, profileId: number) {
  if (!state) {
    return state as unknown as ModuleState;
  }

  const selected = state.profiles.find((profile) => profile.id === profileId);

  return {
    ...state,
    selectedProfileId: profileId,
    selectedProfileName: selected?.name ?? `Profile ${profileId}`,
    rebootRequired: profileId !== state.appliedProfileId,
    profiles: state.profiles.map((profile) => ({
      ...profile,
      selected: profile.id === profileId,
    })),
  };
}

function createDemoStates(): StateMap {
  const profiles = [
    createProfile(0, "UNSPECIFIED", {}, true),
    createProfile(1, "KEY", { kscan: true }, false),
    createProfile(2, "ENC", { encoder: true }, true),
    createProfile(3, "JOY", { adc: true, encoder: true }, true),
    createProfile(4, "TB", { spi: true }, true),
    createProfile(5, "TPD", { i2c: true }, true),
    createProfile(6, "IQS", { i2c: true }, false),
  ];

  return {
    0: createState(0, 2, 2, profiles),
    1: createState(
      1,
      6,
      4,
      profiles.map((profile) => ({
        ...profile,
        available: [0, 4, 5, 6].includes(profile.id),
      })),
    ),
  };
}

function createState(
  source: number,
  selectedProfileId: number,
  appliedProfileId: number,
  profiles: ModuleProfile[],
): ModuleState {
  const selected = profiles.find((profile) => profile.id === selectedProfileId);
  const applied = profiles.find((profile) => profile.id === appliedProfileId);

  return {
    selectedProfileId,
    selectedProfileName: selected?.name ?? "UNKNOWN",
    appliedProfileId,
    appliedProfileName: applied?.name ?? "UNKNOWN",
    applied: selectedProfileId === appliedProfileId,
    rebootRequired: selectedProfileId !== appliedProfileId,
    profiles: profiles.map((profile) => ({
      ...profile,
      selected: profile.id === selectedProfileId,
      applied: profile.id === appliedProfileId,
    })),
    source,
    status: 0,
  };
}

function createProfile(
  id: number,
  name: string,
  capabilities: Partial<Omit<ModuleCapabilities, "flags">>,
  available: boolean,
): ModuleProfile {
  const fullCapabilities = {
    flags: capabilityFlags(capabilities),
    kscan: Boolean(capabilities.kscan),
    encoder: Boolean(capabilities.encoder),
    adc: Boolean(capabilities.adc),
    spi: Boolean(capabilities.spi),
    i2c: Boolean(capabilities.i2c),
  };

  return {
    id,
    name,
    capabilities: fullCapabilities,
    selected: false,
    applied: false,
    available,
  };
}

function capabilityFlags(capabilities: Partial<Omit<ModuleCapabilities, "flags">>) {
  return (
    (capabilities.kscan ? 1 : 0) |
    (capabilities.encoder ? 2 : 0) |
    (capabilities.adc ? 4 : 0) |
    (capabilities.spi ? 8 : 0) |
    (capabilities.i2c ? 16 : 0)
  );
}

export default App;

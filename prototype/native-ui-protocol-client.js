(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  if (root) root.NativeUiProtocolClient = api.NativeUiProtocolClient;
})(typeof window !== "undefined" ? window : globalThis, function () {
  "use strict";

  const MAX_U64 = "18446744073709551615";
  const U64_FIELDS = new Set(["epoch", "request", "sequence"]);
  const COMMAND_FIELDS = {
    SetTrackFader: ["track", "value"],
    SetTrackPan: ["track", "value"],
    SetTrackMute: ["track", "flag"],
    SetTrackSolo: ["track", "flag"],
    SetTrackAuxSend: ["track", "index", "value"],
    SetTrackEqBand: ["track", "index", "frequencyHz", "q", "gainDb"],
    SetMasterFader: ["value"],
    SetMasterMute: ["flag"],
    SetAudioInputPairLinked: ["flag"],
    SetAudioInputPairStereoGain: ["value"],
    SetAudioInputPairBalance: ["value"],
    SetAudioInputPairStereoMute: ["flag"],
    SetAudioInputChannelGain: ["track", "value"],
    SetAudioInputChannelPan: ["track", "value"],
    SetAudioInputChannelMute: ["track", "flag"],
    SetAudioInputChannelSolo: ["track", "flag"],
    SetAudioInputChannelAuxSend: ["track", "index", "value"],
    SetAudioInputChannelEqBand: ["track", "index", "frequencyHz", "q", "gainDb", "sampleRate"]
  };

  function canonicalUint64(value, allowZero) {
    let text;
    if (typeof value === "string") text = value;
    else if (typeof value === "number" && Number.isSafeInteger(value) && value >= 0) text = String(value);
    else return null;
    if (!/^(0|[1-9][0-9]*)$/.test(text)) return null;
    if (text.length > MAX_U64.length ||
        (text.length === MAX_U64.length && text > MAX_U64)) return null;
    if (!allowZero && text === "0") return null;
    return text;
  }

  function compareUint64(a, b) {
    return a.length === b.length ? (a === b ? 0 : a < b ? -1 : 1) : a.length < b.length ? -1 : 1;
  }

  function incrementUint64(value) {
    const digits = value.split("");
    for (let i = digits.length - 1; i >= 0; --i) {
      if (digits[i] !== "9") {
        digits[i] = String(Number(digits[i]) + 1);
        return digits.join("");
      }
      digits[i] = "0";
    }
    return digits.length === MAX_U64.length ? null : "1" + digits.join("");
  }

  // Preserve the protocol's uint64 identifiers exactly. JSON.parse alone
  // rounds integers above Number.MAX_SAFE_INTEGER before a reviver can see them.
  function parseProtocolJson(source) {
    if (typeof source !== "string" || source.length === 0) throw new Error("empty payload");
    const output = [];
    const stack = [];
    let i = 0;
    while (i < source.length) {
      const ch = source[i];
      if (ch === '"') {
        const start = i++;
        while (i < source.length) {
          if (source[i] === "\\") { i += 2; continue; }
          if (source[i++] === '"') break;
        }
        const token = source.slice(start, i);
        let lookahead = i;
        while (lookahead < source.length && /\s/.test(source[lookahead])) lookahead++;
        const top = stack[stack.length - 1];
        if (top && top.type === "object" && source[lookahead] === ":") {
          const key = JSON.parse(token);
          if (top.keys.has(key)) throw new Error("duplicate key");
          top.keys.add(key);
          top.key = key;
        } else if (top && top.type === "object") {
          top.key = null;
        }
        output.push(token);
        continue;
      }
      if (ch === "{" || ch === "[") {
        const parent = stack[stack.length - 1];
        if (parent && parent.type === "object") parent.key = null;
        stack.push(ch === "{"
          ? { type: "object", key: null, keys: new Set() }
          : { type: "array" });
        output.push(ch);
        i++;
        continue;
      }
      if (ch === "}" || ch === "]") {
        stack.pop();
        output.push(ch);
        i++;
        continue;
      }
      if (ch === ",") {
        const top = stack[stack.length - 1];
        if (top && top.type === "object") top.key = null;
        output.push(ch);
        i++;
        continue;
      }
      if (ch === ":") {
        output.push(ch);
        i++;
        continue;
      }
      if (ch === "-" || (ch >= "0" && ch <= "9")) {
        const start = i;
        while (i < source.length && !/[\s,\]}]/.test(source[i])) i++;
        const token = source.slice(start, i);
        const top = stack[stack.length - 1];
        if (top && top.type === "object" && U64_FIELDS.has(top.key) &&
            /^(0|[1-9][0-9]*)$/.test(token)) {
          output.push(JSON.stringify(token));
        } else {
          output.push(token);
        }
        if (top && top.type === "object") top.key = null;
        continue;
      }
      if (ch === "t" || ch === "f" || ch === "n") {
        const start = i;
        while (i < source.length && /[A-Za-z]/.test(source[i])) i++;
        output.push(source.slice(start, i));
        const top = stack[stack.length - 1];
        if (top && top.type === "object") top.key = null;
        continue;
      }
      output.push(ch);
      i++;
    }
    return JSON.parse(output.join(""));
  }

  function encodeProtocolJson(value, key) {
    if (U64_FIELDS.has(key)) {
      const integer = canonicalUint64(value, key === "epoch");
      if (integer === null) throw new Error("invalid uint64 field: " + key);
      return integer;
    }
    if (value === null) return "null";
    if (typeof value === "string") return JSON.stringify(value);
    if (typeof value === "boolean") return value ? "true" : "false";
    if (typeof value === "number") {
      if (!Number.isFinite(value)) throw new Error("non-finite number");
      return JSON.stringify(value);
    }
    if (Array.isArray(value)) return "[" + value.map(item => encodeProtocolJson(item, "")).join(",") + "]";
    if (typeof value === "object") {
      const keys = Object.keys(value);
      return "{" + keys.map(name =>
        JSON.stringify(name) + ":" + encodeProtocolJson(value[name], name)
      ).join(",") + "}";
    }
    throw new Error("unsupported JSON value");
  }

  function exactKeys(value, names) {
    if (!value || typeof value !== "object" || Array.isArray(value)) return false;
    const actual = Object.keys(value);
    return actual.length === names.length && names.every(name =>
      Object.prototype.hasOwnProperty.call(value, name));
  }

  function utf8ByteLength(value) {
    if (typeof TextEncoder !== "undefined") return new TextEncoder().encode(value).length;
    return unescape(encodeURIComponent(value)).length;
  }

  function integer(value, min, max) {
    return Number.isInteger(value) && value >= min && value <= max;
  }

  function finite(value, min, max) {
    return typeof value === "number" && Number.isFinite(value) && value >= min && value <= max;
  }

  function validEq(bands, sampleRate, checkNyquist) {
    if (!Array.isArray(bands) || bands.length !== 6) return false;
    return bands.every(band => exactKeys(band, ["frequency_hz", "q", "gain_db"]) &&
      finite(band.frequency_hz, 20, checkNyquist ? sampleRate / 2 - Number.EPSILON : 24000) &&
      (!checkNyquist || band.frequency_hz < sampleRate / 2) &&
      finite(band.q, 0.1, 10) && finite(band.gain_db, -12, 12));
  }

  function validBoolArray(value, length) {
    return Array.isArray(value) && value.length === length &&
      value.every(item => typeof item === "boolean");
  }

  function validByteArray(value, length, maximum) {
    return Array.isArray(value) && value.length === length &&
      value.every(item => integer(item, 0, maximum));
  }

  function validSnapshotState(state) {
    if (!exactKeys(state, ["tracks", "aux_returns", "master_fader", "master_mute", "audio_input_pair"]) ||
        !Array.isArray(state.tracks) || state.tracks.length !== 16 ||
        !Array.isArray(state.aux_returns) || state.aux_returns.length !== 4 ||
        !integer(state.master_fader, 0, 127) || typeof state.master_mute !== "boolean") return false;

    const validTrack = track => {
      if (!exactKeys(track, ["fader", "pan", "mute", "solo", "aux_sends", "eq",
                             "eq_configured", "eq_sample_rate", "has_insert"]) ||
          !integer(track.fader, 0, 127) || !integer(track.pan, -100, 100) ||
          typeof track.mute !== "boolean" || typeof track.solo !== "boolean" ||
          !validByteArray(track.aux_sends, 4, 100) ||
          !validBoolArray(track.has_insert, 2) ||
          typeof track.eq_configured !== "boolean" ||
          !finite(track.eq_sample_rate, 0, 192000)) return false;
      if (track.eq_configured) {
        return track.eq_sample_rate >= 8000 &&
          validEq(track.eq, track.eq_sample_rate, true);
      }
      return track.eq_sample_rate === 0 && validEq(track.eq, 0, false);
    };

    const validAuxReturn = aux => exactKeys(aux, ["level", "balance", "mute"]) &&
      integer(aux.level, 0, 127) && integer(aux.balance, -100, 100) &&
      typeof aux.mute === "boolean";

    const pair = state.audio_input_pair;
    const validAudioChannel = channel => exactKeys(channel, [
      "gain", "pan", "mute", "solo", "eq_sample_rate", "aux_sends", "eq", "has_insert"
    ]) && integer(channel.gain, 0, 127) && integer(channel.pan, -100, 100) &&
      typeof channel.mute === "boolean" && typeof channel.solo === "boolean" &&
      finite(channel.eq_sample_rate, 8000, 192000) &&
      validByteArray(channel.aux_sends, 4, 100) &&
      validEq(channel.eq, channel.eq_sample_rate, true) &&
      validBoolArray(channel.has_insert, 2);

    return state.tracks.every(validTrack) && state.aux_returns.every(validAuxReturn) &&
      exactKeys(pair, ["linked", "stereo_gain", "balance", "stereo_mute", "stereo_solo",
                       "stereo_aux_sends", "channels"]) &&
      typeof pair.linked === "boolean" && integer(pair.stereo_gain, 0, 127) &&
      integer(pair.balance, -100, 100) && typeof pair.stereo_mute === "boolean" &&
      typeof pair.stereo_solo === "boolean" &&
      validByteArray(pair.stereo_aux_sends, 4, 100) &&
      Array.isArray(pair.channels) && pair.channels.length === 2 &&
      pair.channels.every(validAudioChannel);
  }

  function validateCommand(command) {
    if (!command || typeof command !== "object" || Array.isArray(command) ||
        typeof command.type !== "string" || !COMMAND_FIELDS[command.type]) return false;
    const required = ["type"].concat(COMMAND_FIELDS[command.type]);
    if (command.type === "SetTrackEqBand" &&
        Object.keys(command).length === required.length + 1 &&
        Object.prototype.hasOwnProperty.call(command, "sampleRate")) {
      required.push("sampleRate");
    }
    if (!exactKeys(command, required)) return false;
    const type = command.type;
    const hasTrack = Object.prototype.hasOwnProperty.call(command, "track");
    const trackMax = type.indexOf("SetAudioInputChannel") === 0 ? 1 : 15;
    if (hasTrack && !integer(command.track, 0, trackMax)) return false;
    if (Object.prototype.hasOwnProperty.call(command, "index")) {
      const maximum = type.indexOf("EqBand") >= 0 ? 5 : 3;
      if (!integer(command.index, 0, maximum)) return false;
    }
    if (Object.prototype.hasOwnProperty.call(command, "value")) {
      let min = 0, max = 127;
      if (type === "SetTrackPan" || type === "SetAudioInputChannelPan" ||
          type === "SetAudioInputPairBalance") { min = -100; max = 100; }
      else if (type.indexOf("AuxSend") >= 0) max = 100;
      if (!integer(command.value, min, max)) return false;
    }
    if (Object.prototype.hasOwnProperty.call(command, "flag") &&
        typeof command.flag !== "boolean") return false;
    if (type === "SetTrackEqBand" || type === "SetAudioInputChannelEqBand") {
      const sampleRate = command.sampleRate === undefined ? 48000 : command.sampleRate;
      if (!finite(sampleRate, 8000, 192000) ||
          !finite(command.frequencyHz, 20, sampleRate / 2) ||
          command.frequencyHz >= sampleRate / 2 ||
          !finite(command.q, 0.1, 10) || !finite(command.gainDb, -12, 12)) return false;
      if (type === "SetAudioInputChannelEqBand" && command.sampleRate === undefined) return false;
    }
    return true;
  }

  class NativeUiProtocolClient {
    constructor(options) {
      const config = options || {};
      if (typeof config.send !== "function") throw new TypeError("send callback required");
      this.send = config.send;
      this.clientName = config.clientName || "touch-mixer";
      if (typeof this.clientName !== "string" || this.clientName.length === 0 ||
          utf8ByteLength(this.clientName) > 64) {
        throw new TypeError("clientName must contain 1..64 UTF-8 bytes");
      }
      this.onState = typeof config.onState === "function" ? config.onState : function () {};
      this.onSnapshot = typeof config.onSnapshot === "function" ? config.onSnapshot : function () {};
      this.onCommandResult = typeof config.onCommandResult === "function" ? config.onCommandResult : function () {};
      this.onAudioAck = typeof config.onAudioAck === "function" ? config.onAudioAck : function () {};
      this.onProtocolError = typeof config.onProtocolError === "function" ? config.onProtocolError : function () {};
      this.onDisconnect = typeof config.onDisconnect === "function" ? config.onDisconnect : function () {};
      this.lastSequence = canonicalUint64(config.lastSequence === undefined ? "0" : config.lastSequence, true);
      if (this.lastSequence === null) throw new TypeError("invalid lastSequence");
      this.lastSnapshotSequence = "0";
      this.requestCounter = "0";
      this.outstandingRequest = null;
      this.epoch = "0";
      this.connected = false;
      this.ready = false;
      this.snapshot = null;
      this.pending = new Map();
    }

    state() {
      return {
        connected: this.connected,
        ready: this.ready,
        epoch: this.epoch,
        lastSequence: this.lastSequence,
        outstandingRequest: this.outstandingRequest,
        pendingCommands: this.pending.size
      };
    }

    notifyState() { this.onState(this.state()); }

    sendMessage(message) {
      try { return this.send(encodeProtocolJson(message)) !== false; }
      catch (error) { return false; }
    }

    connect() {
      if (this.connected) return false;
      this.connected = true;
      this.ready = false;
      this.epoch = "0";
      this.requestCounter = "0";
      this.outstandingRequest = null;
      this.snapshot = null;
      this.pending.clear();
      const sent = this.sendMessage({
        version: 1, kind: "hello", epoch: 0, client: this.clientName
      });
      if (!sent) {
        this.connected = false;
        this.notifyState();
        return false;
      }
      this.notifyState();
      return true;
    }

    disconnect() {
      if (!this.connected && this.epoch === "0") return false;
      this.connected = false;
      this.ready = false;
      this.epoch = "0";
      this.outstandingRequest = null;
      this.pending.clear();
      this.onDisconnect();
      this.notifyState();
      return true;
    }

    requestSnapshot() {
      if (!this.connected || this.epoch === "0" || this.outstandingRequest !== null) return false;
      const request = incrementUint64(this.requestCounter);
      if (request === null) return false;
      if (!this.sendMessage({
        version: 1, kind: "snapshot_request", epoch: this.epoch, request
      })) return false;
      this.requestCounter = request;
      this.outstandingRequest = request;
      return true;
    }

    command(command) {
      if (!this.connected || !this.ready || !validateCommand(command)) return null;
      const sequence = incrementUint64(this.lastSequence);
      if (sequence === null) return null;
      const message = Object.assign({
        version: 1, kind: "command", epoch: this.epoch, sequence
      }, command);
      const payload = encodeProtocolJson(message);
      const entry = { payload, status: "sent", audioTicket: 0 };
      this.pending.set(sequence, entry);
      let sent = false;
      try { sent = this.send(payload) !== false; } catch (error) { sent = false; }
      if (!sent) {
        this.pending.delete(sequence);
        return null;
      }
      this.lastSequence = sequence;
      this.notifyState();
      return sequence;
    }

    retryCommand(sequence) {
      const id = canonicalUint64(sequence, false);
      const entry = id === null ? null : this.pending.get(id);
      if (!entry || entry.status !== "queue_full") return false;
      let sent = false;
      try { sent = this.send(entry.payload) !== false; } catch (error) { sent = false; }
      if (sent) entry.status = "sent";
      return sent;
    }

    receive(payload) {
      let message;
      try { message = parseProtocolJson(payload); }
      catch (error) { return false; }
      if (!message || typeof message !== "object" || Array.isArray(message) ||
          !Object.prototype.hasOwnProperty.call(message, "version") ||
          !Object.prototype.hasOwnProperty.call(message, "kind") ||
          !Object.prototype.hasOwnProperty.call(message, "epoch") ||
          message.version !== 1 || typeof message.kind !== "string") return false;
      const epoch = canonicalUint64(message.epoch, true);
      if (epoch === null) return false;

      if (message.kind === "hello") {
        if (!exactKeys(message, ["version", "kind", "epoch", "status"]) ||
            !this.connected || this.epoch !== "0" || epoch === "0" ||
            message.status !== "ready") return false;
        this.epoch = epoch;
        if (!this.requestSnapshot()) {
          this.disconnect();
          return false;
        }
        this.notifyState();
        return true;
      }

      if (!this.connected || this.epoch === "0" || epoch !== this.epoch) return false;

      if (message.kind === "snapshot") {
        if (!exactKeys(message, ["version", "kind", "epoch", "request", "sequence",
                                 "audio_ticket_watermark", "audio_state_caught_up", "state"])) return false;
        const request = canonicalUint64(message.request, false);
        const sequence = canonicalUint64(message.sequence, false);
        if (request === null || sequence === null ||
            request !== this.outstandingRequest ||
            compareUint64(sequence, this.lastSnapshotSequence) <= 0 ||
            !integer(message.audio_ticket_watermark, 0, 4294967295) ||
            typeof message.audio_state_caught_up !== "boolean" ||
            !validSnapshotState(message.state)) return false;
        this.outstandingRequest = null;
        this.lastSnapshotSequence = sequence;
        this.snapshot = message;
        this.ready = message.audio_state_caught_up;
        this.onSnapshot(message);
        this.notifyState();
        return true;
      }

      if (message.kind === "command_result") {
        if (!exactKeys(message, ["version", "kind", "epoch", "sequence", "status", "audio_ticket"]) ||
            !["accepted", "invalid", "out_of_order", "queue_full", "unsupported"].includes(message.status) ||
            !integer(message.audio_ticket, 0, 4294967295)) return false;
        const sequence = canonicalUint64(message.sequence, false);
        const entry = sequence === null ? null : this.pending.get(sequence);
        if (!entry || entry.status !== "sent" ||
            (message.status !== "accepted" && message.audio_ticket !== 0)) return false;
        if (message.status === "accepted" && message.audio_ticket !== 0) {
          entry.status = "awaiting_ack";
          entry.audioTicket = message.audio_ticket;
        } else if (message.status === "queue_full") {
          entry.status = "queue_full";
        } else {
          this.pending.delete(sequence);
        }
        this.onCommandResult(message);
        this.notifyState();
        return true;
      }

      if (message.kind === "audio_ack") {
        if (!exactKeys(message, ["version", "kind", "epoch", "sequence",
                                 "audio_ticket", "status"]) ||
            message.status !== "applied" ||
            !integer(message.audio_ticket, 1, 4294967295)) return false;
        const sequence = canonicalUint64(message.sequence, false);
        const entry = sequence === null ? null : this.pending.get(sequence);
        if (!entry || entry.status !== "awaiting_ack" ||
            entry.audioTicket !== message.audio_ticket) return false;
        this.pending.delete(sequence);
        this.onAudioAck(message);
        this.notifyState();
        return true;
      }

      if (message.kind === "protocol_error") {
        if (!exactKeys(message, ["version", "kind", "epoch", "code", "fatal"]) ||
            !["unsupported_version", "invalid_message", "stale_epoch",
              "not_synchronized", "message_too_large", "server_busy"].includes(message.code) ||
            typeof message.fatal !== "boolean") return false;
        this.onProtocolError(message);
        if (message.fatal) this.disconnect();
        return true;
      }

      return false;
    }
  }

  return { NativeUiProtocolClient, parseProtocolJson, validateCommand, validSnapshotState };
});

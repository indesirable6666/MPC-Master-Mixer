const test = require('node:test');
const assert = require('node:assert/strict');
const {
  NativeUiProtocolClient,
  parseProtocolJson,
  validateCommand,
  validSnapshotState
} = require('../prototype/native-ui-protocol-client.js');

const bands = () => [40, 100, 400, 1000, 4000, 12000].map(frequency_hz => ({
  frequency_hz, q: 0.707, gain_db: 0
}));

function makeTrack() {
  return {
    fader: 100, pan: 0, mute: false, solo: false,
    aux_sends: [0, 0, 0, 0],
    eq: bands(), eq_configured: true, eq_sample_rate: 48000,
    has_insert: [false, false]
  };
}

function makeAudioChannel() {
  return {
    gain: 100, pan: 0, mute: false, solo: false,
    eq_sample_rate: 48000, aux_sends: [0, 0, 0, 0],
    eq: bands(), has_insert: [false, false]
  };
}

function makeSnapshot(epoch, request, sequence, caughtUp = true) {
  return JSON.stringify({
    version: 1, kind: 'snapshot', epoch, request, sequence,
    audio_ticket_watermark: 0, audio_state_caught_up: caughtUp,
    state: {
      tracks: Array.from({ length: 16 }, makeTrack),
      aux_returns: Array.from({ length: 4 }, () => ({
        level: 100, balance: 0, mute: false
      })),
      master_fader: 100, master_mute: false,
      audio_input_pair: {
        linked: false, stereo_gain: 100, balance: 0,
        stereo_mute: false, stereo_solo: false,
        stereo_aux_sends: [0, 0, 0, 0],
        channels: [makeAudioChannel(), makeAudioChannel()]
      }
    }
  });
}

function connectedClient(options = {}) {
  const sent = [];
  const client = new NativeUiProtocolClient({
    send: payload => { sent.push(payload); return true; },
    ...options
  });
  assert.equal(client.connect(), true);
  assert.equal(client.receive(JSON.stringify({
    version: 1, kind: 'hello', epoch: 7, status: 'ready'
  })), true);
  assert.equal(client.receive(makeSnapshot(7, 1, 1, true)), true);
  assert.equal(client.state().ready, true);
  return { client, sent };
}

test('handshake requests a snapshot and readiness waits for caught-up audio state', () => {
  const sent = [];
  const states = [];
  const snapshots = [];
  const client = new NativeUiProtocolClient({
    send: payload => { sent.push(parseProtocolJson(payload)); return true; },
    onState: state => states.push(state),
    onSnapshot: snapshot => snapshots.push(snapshot)
  });

  assert.equal(client.connect(), true);
  assert.deepEqual(sent[0], {
    version: 1, kind: 'hello', epoch: '0', client: 'touch-mixer'
  });
  assert.equal(client.receive(JSON.stringify({
    version: 1, kind: 'hello', epoch: 7, status: 'ready'
  })), true);
  assert.deepEqual(sent[1], {
    version: 1, kind: 'snapshot_request', epoch: '7', request: '1'
  });
  assert.equal(client.state().ready, false);

  assert.equal(client.receive(makeSnapshot(7, 1, 1, false)), true);
  assert.equal(client.state().ready, false);
  assert.equal(snapshots.length, 1);
  assert.equal(client.requestSnapshot(), true);
  assert.equal(client.receive(makeSnapshot(7, 2, 2, true)), true);
  assert.equal(client.state().ready, true);
  assert.equal(states.at(-1).ready, true);
});

test('accepted asynchronous commands remain pending until a matching audio ACK', () => {
  const acknowledgements = [];
  const { client, sent } = connectedClient({
    onAudioAck: message => acknowledgements.push(message)
  });
  const sequence = client.command({ type: 'SetAudioInputPairLinked', flag: true });
  assert.equal(sequence, '1');
  assert.deepEqual(parseProtocolJson(sent.at(-1)), {
    version: 1, kind: 'command', epoch: '7', sequence: '1',
    type: 'SetAudioInputPairLinked', flag: true
  });

  assert.equal(client.receive(JSON.stringify({
    version: 1, kind: 'command_result', epoch: 7, sequence: 1,
    status: 'accepted', audio_ticket: 55
  })), true);
  assert.equal(client.state().pendingCommands, 1);
  assert.equal(client.receive(JSON.stringify({
    version: 1, kind: 'audio_ack', epoch: 7, sequence: 1,
    audio_ticket: 56, status: 'applied'
  })), false);
  assert.equal(client.state().pendingCommands, 1);
  assert.equal(client.receive(JSON.stringify({
    version: 1, kind: 'audio_ack', epoch: 7, sequence: 1,
    audio_ticket: 55, status: 'applied'
  })), true);
  assert.equal(client.state().pendingCommands, 0);
  assert.equal(acknowledgements.length, 1);
});

test('queue-full retry resends the identical sequence and payload', () => {
  const { client, sent } = connectedClient();
  const sequence = client.command({ type: 'SetTrackFader', track: 3, value: 91 });
  assert.equal(sequence, '1');
  const original = sent.at(-1);
  assert.equal(client.receive(JSON.stringify({
    version: 1, kind: 'command_result', epoch: 7, sequence: 1,
    status: 'queue_full', audio_ticket: 0
  })), true);
  assert.equal(client.retryCommand(sequence), true);
  assert.equal(sent.at(-1), original);
  assert.equal(client.lastSequence, '1');
  assert.equal(client.receive(JSON.stringify({
    version: 1, kind: 'command_result', epoch: 7, sequence: 1,
    status: 'accepted', audio_ticket: 0
  })), true);
  assert.equal(client.state().pendingCommands, 0);
  assert.equal(client.retryCommand(sequence), false);
});

test('reconnect rejects stale epochs and preserves the dispatcher sequence', () => {
  const { client, sent } = connectedClient();
  assert.equal(client.command({ type: 'SetTrackMute', track: 0, flag: true }), '1');
  assert.equal(client.receive(JSON.stringify({
    version: 1, kind: 'command_result', epoch: 7, sequence: 1,
    status: 'accepted', audio_ticket: 0
  })), true);
  assert.equal(client.disconnect(), true);
  assert.equal(client.connect(), true);
  assert.equal(client.receive(JSON.stringify({
    version: 1, kind: 'hello', epoch: 8, status: 'ready'
  })), true);
  assert.equal(client.receive(makeSnapshot(8, 1, 2, true)), true);

  assert.equal(client.receive(JSON.stringify({
    version: 1, kind: 'command_result', epoch: 7, sequence: 2,
    status: 'accepted', audio_ticket: 0
  })), false);
  assert.equal(client.command({ type: 'SetTrackFader', track: 1, value: 88 }), '2');
  assert.equal(parseProtocolJson(sent.at(-1)).sequence, '2');
});

test('uint64 epochs and sequences are preserved without JavaScript rounding', () => {
  const sent = [];
  const client = new NativeUiProtocolClient({
    send: payload => { sent.push(payload); return true; }
  });
  assert.equal(client.connect(), true);
  const max = '18446744073709551615';
  assert.equal(client.receive(
    '{"version":1,"kind":"hello","epoch":18446744073709551615,"status":"ready"}'
  ), true);
  const request = parseProtocolJson(sent.at(-1));
  assert.equal(request.epoch, max);
  assert.equal(sent.at(-1).includes('"epoch":' + max), true);
  assert.equal(sent.at(-1).includes('"epoch":"' + max + '"'), false);
  assert.equal(client.state().epoch, max);
  assert.equal(client.requestSnapshot(), false);
});

test('duplicate keys, malformed snapshots and out-of-range commands are rejected', () => {
  const { client } = connectedClient();
  assert.equal(client.receive(
    '{"version":1,"version":1,"kind":"protocol_error","epoch":7,"code":"invalid_message","fatal":false}'
  ), false);

  const invalid = JSON.parse(makeSnapshot(7, 2, 2, true));
  invalid.state.tracks[0].aux_sends.push(12);
  assert.equal(validSnapshotState(invalid.state), false);
  assert.equal(client.requestSnapshot(), true);
  assert.equal(client.receive(JSON.stringify(invalid)), false);
  assert.equal(client.state().ready, true); // The last valid snapshot remains active.

  assert.equal(validateCommand({
    type: 'SetAudioInputChannelGain', track: 2, value: 100
  }), false);
  assert.equal(validateCommand({
    type: 'SetAudioInputChannelEqBand', track: 0, index: 1,
    frequencyHz: 1000, q: 0.7, gainDb: 1
  }), false);
  assert.equal(validateCommand({
    type: 'SetAudioInputChannelEqBand', track: 1, index: 1,
    frequencyHz: 24000, q: 0.7, gainDb: 1, sampleRate: 48000
  }), false);
  assert.equal(client.command({
    type: 'SetAudioInputChannelGain', track: 2, value: 100
  }), null);
});

test('a failed local send does not consume a command sequence', () => {
  let fail = false;
  const sent = [];
  const client = new NativeUiProtocolClient({
    send: payload => {
      if (fail) return false;
      sent.push(payload);
      return true;
    }
  });
  assert.equal(client.connect(), true);
  client.receive(JSON.stringify({ version: 1, kind: 'hello', epoch: 7, status: 'ready' }));
  client.receive(makeSnapshot(7, 1, 1, true));
  fail = true;
  assert.equal(client.command({ type: 'SetMasterFader', value: 90 }), null);
  assert.equal(client.lastSequence, '0');
  fail = false;
  assert.equal(client.command({ type: 'SetMasterFader', value: 90 }), '1');
});

test('fatal protocol errors close the logical client session', () => {
  const { client } = connectedClient();
  assert.equal(client.receive(JSON.stringify({
    version: 1, kind: 'protocol_error', epoch: 7,
    code: 'not_synchronized', fatal: true
  })), true);
  assert.equal(client.state().connected, false);
  assert.equal(client.state().ready, false);
});

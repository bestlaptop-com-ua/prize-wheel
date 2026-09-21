"""Read-only serial audit. Sends only help, status and three q snapshots."""
import argparse
import json
from pathlib import Path
import serial
from run_trial import Recorder, Session, Refusal, number, fields

BUILD = '# build: phase-audit-readonly-20260920; based on pulsefirst-r2'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--log-dir', type=Path, required=True)
    args = parser.parse_args()
    record = Recorder(args.log_dir)
    port = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=.5)
    port.dtr = port.rts = False
    port.port = 'COM7'
    session = Session(port, record)
    result = {'build': BUILD, 'motion_command_sent': False, 'snapshots': []}

    def status():
        data = session.status()
        state, motor, test = (data[k] for k in ('state', 'motor', 'selfspin'))
        if not (number(test, 'motion_compiled') == 0 and number(test, 'active') == 0
                and number(test, 'inhibit') == 1 and number(test, 'recovery_pending') == 0
                and number(motor, 'EN') == 1 and number(motor, 'current') == 0
                and abs(number(motor, 'fas')) < .001 and number(state, 'stage') == 0
                and number(state, 'takeover') == 0 and state.get('pos') == 'FRESH'
                and state.get('vel') == 'VALID' and abs(number(state, 'omega')) <= .020
                and state.get('state') == 'FAULT_LATCHED'
                and state.get('fault') == 'SELFSPIN_ABORT'):
            raise Refusal('Expected disabled stationary firmware with preserved fault15')
        return data

    try:
        port.open()
        session.drain_existing()
        mark = session.send('?')
        session.wait_line(lambda s: s == BUILD, mark, 3)
        session.read_for(2)
        result['before'] = status()
        for _ in range(3):
            session.read_for(2)
            mark = session.send('q')
            _, first = session.wait_line(lambda s: s.startswith('# AUDIT_BEGIN ')
                                        or s.startswith('# AUDIT refused:'), mark, 3)
            if first.startswith('# AUDIT refused:'):
                raise Refusal(first)
            _, last = session.wait_line(lambda s: s.startswith('# AUDIT_END '), mark, 3)
            lines = [s for _, s in record.lines[session.command_line_start:]
                     if s.startswith('# AUDIT_')]
            values = fields(first)
            if not (values.get('disabled_unchanged') == '1' and values.get('fault') == '15'
                    and values.get('saved_fault_raw') == '15' and values.get('recovery_guard') == '0'):
                raise Refusal('Audit interlock or saved-fault mismatch')
            result['snapshots'].append(lines)
        session.read_for(2)
        result['after'] = status()
        result['success'] = True
    except BaseException as exc:
        result.update(success=False, error=repr(exc))
    finally:
        if port.is_open:
            port.close()
        (record.directory / 'result.json').write_text(json.dumps(result, indent=2))
        record.close()
    print(json.dumps(result, indent=2))
    return 0 if result.get('success') else 1

if __name__ == '__main__':
    raise SystemExit(main())

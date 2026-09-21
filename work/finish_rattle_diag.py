from pathlib import Path
root=Path(__file__).parent.parent
p=root/'prize_wheel_gpt'/'pw_party.h'
s=p.read_bytes().decode('utf-8');eol='\r\n' if '\r\n' in s else '\n'
a='  int available() { return ::Serial.available(); }'
assert s.count(a)==1
s=s.replace(a,'  int availableForWrite() override { return ::Serial.availableForWrite(); }'+eol+a)
p.write_bytes(s.encode('utf-8'))
p=root/'prize_wheel_gpt'/'prize_wheel_gpt.ino'
s=p.read_bytes().decode('utf-8').replace('\r\n','\n')
a="    case 'd':\n      if (!diagnosticCapture) startDiagnosticCapture();"
b='''    case 'D':
      if (diagnosticDumpActive) break;
      if ((state != ST_IDLE_STOPPED && state != ST_FAULT_LATCHED) ||
          currentStage != CS_FREEWHEEL || digitalRead(PIN_EN) != HIGH ||
          (stepper && stepper->isRunning()) || !encoderMotionReady() ||
          fabsf(omega) > STILL_REV_S) {
        Serial.println(F("# DIAG manual dump refused: wheel must be stopped with outputs disabled"));
        break;
      }
      diagnosticFrozen = true;
      diagnosticCapture = false;
      dumpDiagnostics();
      break;
    case 'd':
      if (!diagnosticCapture) startDiagnosticCapture();'''
assert s.count(a)==1;s=s.replace(a,b)
a='    " d  arm high-rate RAM capture (dumps after true stop/fault)\\n"'
assert s.count(a)==1;s=s.replace(a,a+'\n    " D  dump current trace at rest with outputs disabled\\n"')
p.write_bytes(s.replace('\n','\r\n').encode('utf-8'))
print('Added UART write-capacity forwarding and guarded manual diagnostic dump.')

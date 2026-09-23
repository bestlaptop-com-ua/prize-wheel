p=open('apply_imbalance.py').read()
p=p.replace("'const uint16_t CUR_CAPTURE_MA   = 2800; // 2026-09-22: 2240 proved 20% headroom; party runs at 2800\\n'\n     'const uint16_t CUR_BRAKE_MA     = 2800; // retain capture torque through braking/settling\\n')",
"'const uint16_t CUR_CAPTURE_MA   = 2200; // 2026-09-22: wheel balanced; 2240 already had margin unbalanced. Less regen/heat.\\n'\n     'const uint16_t CUR_BRAKE_MA     = 2200; // retain capture torque through braking/settling\\n')")
p=p.replace("'const uint16_t CUR_HOLD1_MA     = 1650; // continuous hold: gravity (0.57 rad/s2) beats friction 3:1\\n'",
"'const uint16_t CUR_HOLD1_MA     = 800;  // continuous hold; balanced wheel needs little, driver stays cool\\n'")
j=p.index("def main():")
new=r'''edit('auto-reconfig after driver power blip',
     'bool captureDriverHealthy() {\n'
     '  const uint32_t drv = driver.DRV_STATUS();\n'
     '  const uint8_t gst = (uint8_t)driver.GSTAT();\n',
     'bool captureDriverHealthy() {\n'
     '  uint32_t drv = driver.DRV_STATUS();\n'
     '  uint8_t gst = (uint8_t)driver.GSTAT();\n'
     '  // 2026-09-22: a supply blip resets the driver (GSTAT reset=1, config lost).\n'
     '  // With outputs OFF that is recoverable: re-apply the config and re-read,\n'
     '  // instead of abandoning every capture until someone types r.\n'
     '  if (gst == 0x01 && driver.version() == 0x30 && digitalRead(PIN_EN) == HIGH) {\n'
     '    driverConfig();\n'
     '    Serial.println(F("# driver reset flag seen with outputs off: config re-applied"));\n'
     '    drv = driver.DRV_STATUS();\n'
     '    gst = (uint8_t)driver.GSTAT();\n'
     '  }\n')


'''
p=p[:j]+new+p[j:]
open('apply_imbalance.py','w').write(p)

@echo off
rem Guest side of the VM audio probe (tools/proto/run_audio_configs.sh). Everything goes to COM1.
echo I7AUDIO begin > COM1
"%~dp0audprobe.exe" poll 90 > COM1
sc query AudioSrv > COM1
sc qc AudioSrv > COM1
sc query AudioEndpointBuilder > COM1
sc qc AudioEndpointBuilder > COM1
echo I7AUDIO pnp-media > COM1
pnputil /enum-devices /class Media > COM1
echo I7AUDIO pnp-endpoint > COM1
pnputil /enum-devices /class AudioEndpoint > COM1
echo I7AUDIO pnp-problem > COM1
pnputil /enum-devices /problem > COM1
echo I7AUDIO drivers > COM1
dir /b C:\Windows\System32\drivers\hdaudbus.sys C:\Windows\System32\drivers\hdaudio.sys C:\Windows\System32\drivers\usbaudio.sys C:\Windows\System32\drivers\portcls.sys > COM1 2>&1
echo I7AUDIO svc-start > COM1
sc start AudioEndpointBuilder > COM1
sc start AudioSrv > COM1
"%~dp0audprobe.exe" poll 20 > COM1
echo I7AUDIO full > COM1
"%~dp0audprobe.exe" full > COM1
echo I7AUDIO end > COM1

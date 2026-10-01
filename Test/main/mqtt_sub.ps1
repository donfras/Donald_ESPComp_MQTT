param([string]$topic='sensors/pumps')
$mqtt_sub = "C:\Program Files\mosquitto\mosquitto_sub.exe"
# Start-Process -FilePath $mqtt_sub -ArgumentList "-h 192.168.1.211 -t sensors/temperature -q 1"
$arg1 = '-h'
$arg2 = 'pimqttserver'
$arg3 = '-t'
$arg4 = $topic
$arg5 = '-q'
$arg6 = '1'
&$mqtt_sub $arg1 $arg2 $arg3 $arg4 $arg5 $arg6 | ForEach-Object {
    "{0} {1}" -f (get-date), $_
    # $output
}

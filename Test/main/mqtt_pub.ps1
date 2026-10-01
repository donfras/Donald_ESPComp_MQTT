param([string]$topic='sensors/commands', [string]$message)
$mqtt_pub = "C:\Program Files\mosquitto\mosquitto_pub.exe"
$arg1 = '-h'
$arg2 = 'pimqttserver'
$arg3 = '-t'
$arg4 = $topic
$arg5 = '-q'
$arg6 = '1'
$arg7 = '-m'
$arg8 = $message
&$mqtt_pub $arg1 $arg2 $arg3 $arg4 $arg5 $arg6 $arg7 $arg8
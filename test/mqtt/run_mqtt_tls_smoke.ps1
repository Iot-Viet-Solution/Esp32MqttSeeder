param(
    [string]$HostName = "localhost",
    [int]$Port = 8884,
    [string]$Topic = "test/esp32/tls",
    [string]$Thumbprint = "",
    [ValidateSet("sha256", "sha1")]
    [string]$ThumbprintAlg = "sha256",
    [string]$ThumbprintFile = "",
    [switch]$NoClientCert,
    [switch]$Insecure,
    [switch]$NoVerify,
    [switch]$NegativeWrongCa
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..\..")
$python = Join-Path $repoRoot ".venv\Scripts\python.exe"
$testScript = Join-Path $repoRoot "test\mqtt\test_mqtt_tls.py"

if (-not (Test-Path $python)) {
    Write-Error "Python executable not found at $python. Configure the virtual environment first."
    exit 2
}

$argsList = @(
    $testScript,
    "--host", $HostName,
    "--port", $Port,
    "--topic", $Topic
)

if ($NoClientCert) {
    $argsList += "--no-client-cert"
}
if ($Thumbprint) {
    $argsList += @("--thumbprint", $Thumbprint)
}
if ($ThumbprintAlg) {
    $argsList += @("--thumbprint-alg", $ThumbprintAlg)
}
if ($ThumbprintFile) {
    $argsList += @("--thumbprint-file", $ThumbprintFile)
}
if ($Insecure) {
    $argsList += "--insecure"
}
if ($NoVerify) {
    $argsList += "--no-verify"
}
if ($NegativeWrongCa) {
    $argsList += "--negative-wrong-ca"
}

Write-Host "Running MQTT TLS smoke test..."
Write-Host "Endpoint: ${HostName}:$Port"
& $python @argsList
exit $LASTEXITCODE

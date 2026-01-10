# Test Prometheus Metrics Endpoint
# This PowerShell script tests the /metrics endpoint

Write-Host "🧪 MetricMQ Prometheus Metrics Test" -ForegroundColor Cyan
Write-Host "=" * 50

# Wait a bit for server to start
Start-Sleep -Seconds 2

try {
    Write-Host "`n📊 Fetching metrics from http://localhost:9091/metrics...`n" -ForegroundColor Yellow
    
    $response = Invoke-WebRequest -Uri "http://localhost:9091/metrics" -UseBasicParsing
    
    Write-Host "✅ HTTP Status: $($response.StatusCode)" -ForegroundColor Green
    Write-Host "✅ Content-Type: $($response.Headers['Content-Type'])" -ForegroundColor Green
    Write-Host "✅ Content-Length: $($response.Content.Length) bytes" -ForegroundColor Green
    
    Write-Host "`n📈 Metrics Output:" -ForegroundColor Cyan
    Write-Host "=" * 50
    Write-Host $response.Content
    
    Write-Host "`n✅ Metrics endpoint test PASSED!" -ForegroundColor Green
    
} catch {
    Write-Host "`n❌ Failed to fetch metrics: $_" -ForegroundColor Red
    Write-Host "Make sure the broker is running!" -ForegroundColor Yellow
}

Write-Host "`n💡 Tip: You can also test with:" -ForegroundColor Yellow
Write-Host "   curl http://localhost:9091/metrics" -ForegroundColor Cyan
Write-Host "   or open http://localhost:9091/metrics in your browser" -ForegroundColor Cyan

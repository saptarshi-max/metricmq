$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
Set-Location 'C:\Users\Sapta\Documents\Projects\MetricMQ'
& $msbuild MetricMQ.sln /p:Configuration=Release /p:Platform=x64 /m /v:minimal 2>&1 | Tee-Object -FilePath build_out.txt

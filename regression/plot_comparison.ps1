param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms.DataVisualization
Add-Type -AssemblyName System.Drawing
$rows = Import-Csv -LiteralPath (Join-Path $Folder 'comparison.csv') -Encoding UTF8
$groups = @($rows | Group-Object variable)
$index = 0
foreach ($group in $groups) {
    $index++
    $chart = New-Object System.Windows.Forms.DataVisualization.Charting.Chart
    try {
        $chart.Width = 1100; $chart.Height = 720
        $chart.BackColor = [System.Drawing.Color]::White
        $title = $chart.Titles.Add($group.Name + ' | matched timestamps only')
        $title.Font = New-Object System.Drawing.Font('Segoe UI',14)
        $legend = New-Object System.Windows.Forms.DataVisualization.Charting.Legend('Legend')
        $legend.Docking = 'Top'; $chart.Legends.Add($legend)
        foreach ($panel in 0,1) {
            $area = New-Object System.Windows.Forms.DataVisualization.Charting.ChartArea("panel$panel")
            $area.Position.X = 2; $area.Position.Width = 96
            $area.Position.Y = 12 + 43*$panel; $area.Position.Height = 43
            $area.AxisX.Minimum = [double]$group.Group[0].time
            $area.AxisX.Maximum = [double]$group.Group[-1].time
            $area.AxisX.Title = 'Time (s)'
            $area.AxisX.LabelStyle.Format = 'G5'
            $area.AxisY.LabelStyle.Format = 'G4'
            $area.AxisY.IsStartedFromZero = $false
            $area.AxisX.MajorGrid.LineColor = [System.Drawing.Color]::Gainsboro
            $area.AxisY.MajorGrid.LineColor = [System.Drawing.Color]::Gainsboro
            $columns = @('actual','reference')
            $area.AxisY.Title = $group.Name
            if ($panel -eq 1) {
                $columns = @('abs_error')
                $area.AxisY.Title = 'Absolute error'
                $area.AxisY.IsStartedFromZero = $true
                if ($group.Group[0].allowed_error -ne '') {$columns += 'allowed_error'}
            }
            # Scale explicitly to avoid the chart library's Decimal limit on tiny or huge values.
            $peak = 0.0
            foreach ($r in $group.Group) {
                foreach ($column in $columns) {$peak = [Math]::Max($peak,[Math]::Abs([double]$r.$column))}
            }
            $scale = 1.0
            if ($peak -gt 0 -and ($peak -lt 0.0001 -or $peak -ge 1000000)) {
                $power = [Math]::Floor([Math]::Log10($peak))
                $scale = [Math]::Pow(10,$power)
                $area.AxisY.Title += ' / 1e' + $power
            }
            $chart.ChartAreas.Add($area)
            foreach ($column in $columns) {
                $series = New-Object System.Windows.Forms.DataVisualization.Charting.Series($column)
                $series.ChartArea = $area.Name
                $series.ChartType = 'Line'; $series.BorderWidth = 2
                $series.IsVisibleInLegend = ($panel -eq 0)
                $series.Color = [System.Drawing.Color]::FromArgb(25,96,175)
                if ($column -in @('reference','allowed_error')) {
                    $series.Color = [System.Drawing.Color]::FromArgb(210,91,25)
                    $series.BorderDashStyle = 'Dash'
                }
                foreach ($r in $group.Group) {
                    [void]$series.Points.AddXY([double]$r.time,([double]$r.$column/$scale))
                }
                $chart.Series.Add($series)
            }
        }
        $path = Join-Path $Folder ('comparison-{0:D2}.png' -f $index)
        $chart.SaveImage($path,[System.Windows.Forms.DataVisualization.Charting.ChartImageFormat]::Png)
    } finally {$chart.Dispose()}
}

<html>
<body>
<!--StartFragment--><html><head></head><body><h1>CalibStudio v1.0.0</h1>
<p><strong>A lightweight Windows tool for viewing and tuning USB and GigE Vision cameras, with no vendor SDK required.</strong></p>
<p>CalibStudio finds your cameras, fixes GigE network settings automatically, and streams several cameras side by side in a dark, distraction-free workspace, with low latency from the sensor to the screen.</p>
<hr>
<img width="1919" height="1008" alt="Screenshot 2026-11-09 192915" src="https://github.com/user-attachments/assets/0a4dde05-9c8f-4c98-af24-6eeb3934e92f" />
<h2>✨ Highlights</h2>
<h3>📷 Cameras</h3>
<ul>
<li><strong>USB / UVC</strong> cameras via DirectShow, with all controls the camera exposes</li>
<li><strong>GigE Vision</strong> cameras <strong>without any vendor SDK</strong>: discovery, GenICam feature access and GVSP streaming are built in</li>
<li><strong>Automatic IP fix.</strong> If a GigE camera sits in a different subnet, CalibStudio assigns it a compatible address (FORCEIP) and connects</li>
<li><strong>Auto-reconnect</strong> when a cable is unplugged or a camera restarts</li>
</ul>
<h3>🖥️ Multi-camera workspace</h3>
<ul>
<li>Several cameras <strong>in parallel</strong>, each in its own window and its own thread</li>
<li>Turn any camera on or off independently</li>
<li>Custom frameless camera windows: tile, cascade, maximize, float</li>
<li>Consistent <strong>dark theme</strong> across the whole interface</li>
</ul>
<h3>🎛️ Exposure &amp; gain</h3>
<p>Works the same for USB and GigE cameras:</p>

Mode | What it does
-- | --
Manual | You set the value with the slider
Once | CalibStudio adjusts brightness to the target, then switches back to Manual
Continuous | CalibStudio keeps the image at the target brightness as the light changes
Auto | The camera's own built-in automatic mode


<p>To verify it in PowerShell:</p>
<pre><code class="language-powershell">Get-FileHash .\CalibStudio-win64.zip -Algorithm SHA256
</code></pre></body></html><!--EndFragment-->
</body>
</html>

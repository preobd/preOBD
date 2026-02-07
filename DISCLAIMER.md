# preOBD Disclaimer and Safety Information

## Software Status

preOBD is **beta software** under active development. It may contain bugs, incomplete features, and undiscovered issues.

## Intended Use

preOBD is designed for:
- Classic cars and vehicles without factory engine management systems
- Supplemental monitoring alongside existing mechanical gauges
- Enthusiast and experimental use
- Educational purposes

preOBD is **NOT** designed for:
- Safety-critical applications
- Primary engine monitoring (use mechanical gauges as primary)
- Modern vehicles with existing ECUs or OBDII systems
- Commercial or certified applications
- Any application where failure could result in injury or significant damage

## User Responsibilities

By using preOBD, you acknowledge and accept:

1. **This software is provided "as is" without warranty of any kind**
2. **You are responsible for monitoring your engine** and preventing damage
3. **Electronic monitoring can fail** - maintain mechanical backup gauges
4. **You must test thoroughly** before trusting any readings
5. **You assume all risk** from use of this software

## Safety Requirements

### Mandatory Requirements

- **Maintain mechanical backup gauges** for critical parameters (oil pressure, coolant temperature)
- **Test extensively** against known-good instruments before trusting readings
- **Verify wiring thoroughly** - incorrect wiring can damage sensors, microcontroller, or vehicle systems
- **Use proper fusing and circuit protection** on all power connections
- **Monitor actively** during initial testing and operation

### Electrical Safety

- **Verify voltage compatibility** - 3.3V boards (Teensy, Due, ESP32) are NOT 5V tolerant
- **Use appropriate voltage dividers** for all inputs exceeding board voltage
- **Check polarity** before connecting power
- **Fuse all power connections** appropriately
- **Disconnect battery** before working on alternator or high-voltage systems

### Installation Safety

- **Route wiring away from** hot exhaust, moving parts, and sharp edges
- **Use automotive-grade wire and connectors**
- **Secure all connections** to withstand vibration
- **Protect electronics** from moisture and contamination
- **Mount securely** in a vibration-resistant location

## Limitations

### Known Limitations

- Sensor calibrations are based on manufacturer specifications and may not match your specific sensor
- ADC accuracy is limited by microcontroller specifications
- Software timing may vary with different configurations
- CAN bus implementation provides monitoring only, not control
- No self-diagnostics or automatic calibration
- Limited to monitoring functions - provides no engine control

### No Guarantees

- **No guarantee of accuracy** - verify readings against known-good instruments
- **No guarantee of reliability** - electronic systems can fail
- **No guarantee of compatibility** - test with your specific hardware
- **No guarantee of fitness** for any particular purpose

## Legal

### Warranty Disclaimer

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.

### Limitation of Liability

IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

### Vehicle Warranty

Installation of aftermarket electronic equipment may void vehicle warranties. Consult your vehicle manufacturer before installation.

## Your Responsibility

**You are solely responsible for:**
- Proper installation and wiring
- Verification of readings
- Maintenance of backup instruments
- Safe operation of your vehicle
- Compliance with local laws and regulations
- Any consequences of sensor failures or incorrect readings

**If you do not accept these terms, do not use this software.**

## Recommended Practices

While not required, we strongly recommend:

1. **Start simple** - Begin with one or two sensors and verify operation
2. **Cross-check readings** - Compare against mechanical gauges or known values
3. **Test at various conditions** - Idle, cruise, full temperature range
4. **Document your installation** - Take notes and photos for troubleshooting
5. **Set conservative alarms** - Better to warn early than fail to warn
6. **Inspect regularly** - Check wiring, connections, and sensor mounting
7. **Have a backup plan** - Know what to do if monitoring fails

## Support Limitations

Community support is provided on a best-effort basis through GitHub:
- Issues and bug reports
- Discussions and questions
- Feature requests

**No guarantee of support or response time.** This is a community project maintained by volunteers.

## Contact

- **Issues:** https://github.com/preobd/preOBD/issues
- **Discussions:** https://github.com/preobd/preOBD/discussions
- **Documentation:** See docs/ folder

---

**Bottom line: Use preOBD responsibly, maintain backups, test thoroughly, and accept that you are responsible for your engine's safety.**

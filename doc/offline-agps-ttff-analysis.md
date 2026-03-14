# Offline A-GPS Assistance for nRF9151: Data Pipeline, Physical Models, and Time-To-First-Fix Analysis

Abstract --- This paper describes the physical and mathematical
foundations of an offline Assisted GPS (A-GPS) data pipeline targeting the
Nordic Semiconductor nRF9151 modem. The system downloads broadcast
navigation data from public GNSS archives (BKG IGS BRDC, USCG NAVCEN
almanac), parses it into a portable double-precision representation, then
converts it to GPS Interface Control Document (IS-GPS-200) scaled integers
for injection into the nRF modem. We present the complete chain of orbital
mechanics, clock correction models, ionospheric delay estimation, and
coordinate transformations involved. We then derive a Time-To-First-Fix
(TTFF) estimation model that quantifies the acquisition search-space
reduction achieved by each category of assistance data --- ephemeris,
almanac, ionospheric corrections, UTC parameters, reference time, and
reference location --- and estimate the resulting TTFF under several
operational scenarios. Finally, we analyze how data freshness (age of
ephemeris, age of almanac) degrades positioning accuracy and inflates
TTFF, providing guidance for refresh intervals in deployed IoT systems.


# GPS Signal Structure and Acquisition Problem

## The C/A Code and Correlation Search

Each GPS satellite transmits a unique Gold code (C/A code) of length 1023
chips at a chipping rate of 1.023 MHz, giving a code period of exactly
1 ms. The receiver must determine two unknowns for each satellite:

- Code phase $\tau$: the alignment of the local replica within the
  1023-chip code period (resolution 0.5 chips is standard for serial
  search acquisition, giving 2046 search bins; see Kaplan & Hegarty [9]
  Section 5.2).
- Carrier frequency offset $f_d$: the apparent Doppler shift of the
  L1 carrier (1575.42 MHz) caused by satellite motion, receiver motion,
  and local oscillator error.

The maximum Doppler shift from satellite motion is derived from the
orbital velocity. For a GPS satellite at orbital radius
$R = 26{,}560$ km, circular velocity is
$v = \sqrt{\mu/R} = \sqrt{3.986 \times 10^{14} / 26{,}560{,}000} \approx 3{,}874$ m/s
(IS-GPS-200 [1]). The maximum line-of-sight velocity toward a ground observer
occurs near the horizon. From the satellite--receiver geometry,
$v_{\max} = v \cdot R_{\text{earth}} \cos(e_{\min}) / R \approx 3{,}874 \times 6{,}371 \times \cos(5°) / 26{,}560 \approx 929$ m/s for a
5-degree elevation mask (Kaplan & Hegarty [9] ch. 2), giving a maximum Doppler of
$f_{d,\max} = v_{\max}/\lambda_{L1} = 929/0.1903 \approx \pm 4{,}880$ Hz,
where $\lambda_{L1} = c/f_{L1} = 0.1903$ m. A receiver's local oscillator adds further uncertainty: for a TCXO with
3 ppm frequency tolerance (a typical specification for low-cost GNSS
receivers, see Kaplan & Hegarty [9] ch. 8), the offset at L1 is
$3 \times 10^{-6} \times 1{,}575.42 \times 10^{6} \approx \pm 4{,}726$ Hz. The total Doppler uncertainty is therefore approximately
$\pm 10$ kHz.

The null-to-null bandwidth of the correlation response is
$1/T_{\text{coh}} = 1/1\text{ ms} = 1000$ Hz. The standard frequency
bin spacing is half this value, 500 Hz, to limit worst-case straddling
loss to $<$ 1 dB (Kaplan & Hegarty [9] ch. 5). Over the
$\pm$10 kHz total uncertainty this yields
$20{,}000 / 500 = 40$ frequency bins.

The total search space per satellite is therefore:

$$N_{\text{bins}} = N_{\tau} \times N_f \approx 2046 \times 40 = 81{,}840$$

With a dwell time of $T_d$ milliseconds per bin and a detection
probability $P_d$ per dwell, the mean acquisition time for a single
satellite is:

$$T_{\text{acq}} \approx \frac{N_{\text{bins}} \cdot T_d}{P_d}$$

For a correlator using 1 ms coherent integration (one C/A code period),
the dwell time per bin is $T_d = 1$ ms. Detection probability per dwell
depends on C/N0; for a typical outdoor GPS signal at 42 dB-Hz with
verification, $P_d \approx 0.5$ (Kaplan & Hegarty [9] ch. 5). A
single-channel receiver would then need $81{,}840 \times 0.001 / 0.5 \approx 164$ s per satellite on average. Since the receiver must
acquire at least 4 satellites for a 3D fix, and must search all 32 PRNs
without knowing which are visible, the cold-start TTFF becomes:

$$T_{\text{cold}} = \frac{32 \times N_{\text{bins}} \times T_d}{N_{\text{ch}} \times P_d}$$

where $N_{\text{ch}}$ is the number of parallel correlator channels.
The nRF9151 product specification [5] does not disclose the exact number
of hardware correlator channels.


## How Assistance Data Reduces the Search Space

Assistance data acts on both dimensions of the search:

| Assistance element | Reduces code-phase bins | Reduces frequency bins | Reduces SV set |
|--------------------|:-----------------------:|:----------------------:|:--------------:|
| Reference time      | Moderate                | ---                   | ---            |
| Reference location  | Strong                  | Moderate              | Strong         |
| Almanac             | ---                     | Strong                | Strong         |
| Ephemeris           | ---                     | Very strong           | Strong         |
| Ionospheric model   | Marginal                | ---                   | ---            |

The key insight is that knowing which satellites are visible (from
almanac or ephemeris + location) eliminates searching the ~20 non-visible
PRNs, and knowing the Doppler shift (from ephemeris + location + time)
collapses the 40-bin frequency search to 2--4 bins.


# Keplerian Orbital Model

## Ephemeris Parameters

The GPS broadcast ephemeris describes each satellite's orbit using a
perturbed Keplerian model. The six classical orbital elements, along with
their secular and harmonic corrections, are broadcast every 30 seconds in
the navigation message. The ephemeris parameter set, as stored in the
application's `struct gps_ephemeris`, comprises:

Shape and size:
- $\sqrt{A}$ --- square root of the semi-major axis (m$^{1/2}$)
- $e$ --- orbital eccentricity (dimensionless)

Orientation:
- $i_0$ --- inclination at reference epoch $t_{oe}$ (rad)
- $\Omega_0$ --- longitude of ascending node at weekly epoch (rad)
- $\omega$ --- argument of perigee (rad)

Position in orbit:
- $M_0$ --- mean anomaly at reference epoch $t_{oe}$ (rad)

Secular rates:
- $\Delta n$ --- mean motion correction (rad/s)
- $\dot{\Omega}$ --- rate of right ascension (rad/s)
- $\dot{i}$ --- rate of inclination (rad/s)

Harmonic corrections (6 coefficients):
- $C_{us}$, $C_{uc}$ --- corrections to argument of latitude (rad)
- $C_{is}$, $C_{ic}$ --- corrections to inclination (rad)
- $C_{rs}$, $C_{rc}$ --- corrections to orbital radius (m)


## Satellite Position Computation (IS-GPS-200 Algorithm)

Given the ephemeris and a time $t$, the satellite ECEF position is
computed by the following procedure (IS-GPS-200, Section 20.3.3.4.3):

**Step 1: Time from ephemeris epoch.**

$$t_k = t - t_{oe}$$

with week crossover correction: if $t_k > 302{,}400$ s then
$t_k = t_k - 604{,}800$; if $t_k < -302{,}400$ s then
$t_k = t_k + 604{,}800$.

**Step 2: Mean anomaly.**

The corrected mean motion is:

$$n = n_0 + \Delta n$$

where $n_0 = \sqrt{\mu / A^3}$ is the computed mean motion and
$\mu = 3.986005 \times 10^{14}$ m$^3$/s$^2$ is the WGS-84 gravitational
parameter (IS-GPS-200 [1] Table 20-IV). The mean anomaly at time $t$ is:

$$M_k = M_0 + n \cdot t_k$$

**Step 3: Eccentric anomaly (Kepler's equation).**

Solve iteratively:

$$E_k = M_k + e \sin E_k$$

Starting with $E_k^{(0)} = M_k$ and iterating until convergence
($|E_k^{(i+1)} - E_k^{(i)}| < 10^{-12}$ rad).

**Step 4: True anomaly.**

$$\nu_k = \text{atan2}\!\left(\frac{\sqrt{1 - e^2}\,\sin E_k}{1 - e\cos E_k},\; \frac{\cos E_k - e}{1 - e\cos E_k}\right)$$

**Step 5: Argument of latitude.**

$$\Phi_k = \nu_k + \omega$$

**Step 6: Harmonic corrections.**

$$\delta u_k = C_{us} \sin 2\Phi_k + C_{uc} \cos 2\Phi_k$$

$$\delta r_k = C_{rs} \sin 2\Phi_k + C_{rc} \cos 2\Phi_k$$

$$\delta i_k = C_{is} \sin 2\Phi_k + C_{ic} \cos 2\Phi_k$$

**Step 7: Corrected orbital elements.**

$$u_k = \Phi_k + \delta u_k$$

$$r_k = A(1 - e\cos E_k) + \delta r_k$$

$$i_k = i_0 + \dot{i}\,t_k + \delta i_k$$

**Step 8: Positions in the orbital plane.**

$$x'_k = r_k \cos u_k, \qquad y'_k = r_k \sin u_k$$

**Step 9: Corrected longitude of ascending node.**

$$\Omega_k = \Omega_0 + (\dot{\Omega} - \dot{\Omega}_e)\,t_k - \dot{\Omega}_e\,t_{oe}$$

where $\dot{\Omega}_e = 7.2921151467 \times 10^{-5}$ rad/s is the
WGS-84 Earth rotation rate (IS-GPS-200 [1] Table 20-IV).

**Step 10: Earth-fixed coordinates.**

$$x_k = x'_k \cos\Omega_k - y'_k \cos i_k \sin\Omega_k$$

$$y_k = x'_k \sin\Omega_k + y'_k \cos i_k \cos\Omega_k$$

$$z_k = y'_k \sin i_k$$


## Satellite Clock Correction Model

The satellite clock offset from GPS system time is modeled as a
second-order polynomial:

$$\Delta t_{SV} = a_{f0} + a_{f1}(t - t_{oc}) + a_{f2}(t - t_{oc})^2 + \Delta t_r$$

where $a_{f0}$, $a_{f1}$, $a_{f2}$ are the clock bias, drift, and drift
rate respectively, and $\Delta t_r$ is the relativistic correction:

$$\Delta t_r = F \cdot e \cdot \sqrt{A} \cdot \sin E_k$$

with $F = -2\sqrt{\mu} / c^2 = -4.442807633 \times 10^{-10}$
s/m$^{1/2}$ (IS-GPS-200 [1] §20.3.3.3.3.1).

The total group delay $T_{GD}$ corrects for the inter-frequency bias of
the L1 signal.


# Almanac: Reduced-Precision Orbital Model

## Purpose and Precision Trade-off

The GPS almanac is a reduced-precision subset of the ephemeris, designed
to give the receiver coarse knowledge of the entire constellation. While
ephemeris has a nominal fit interval of 4 hours (IS-GPS-200 [1] Section
20.3.4.4) and achieves sub-meter URE within that window (GPS SPS
Performance Standard [11]), the almanac is valid for days to weeks and
provides kilometer-level orbit predictions (Kaplan & Hegarty [9]
Section 2.5).

The key differences in precision:

| Parameter         | Ephemeris bits | Ephemeris LSB      | Almanac bits | Almanac LSB        | Precision ratio |
|-------------------|:--------------:|:------------------:|:------------:|:------------------:|:---------------:|
| $\sqrt{A}$        | 32 unsigned    | $2^{-19}$ m$^{1/2}$ | 24 unsigned | $2^{-11}$ m$^{1/2}$ | 256x coarser    |
| $e$               | 32 unsigned    | $2^{-33}$          | 16 unsigned  | $2^{-21}$          | 4096x coarser   |
| $i_0$             | 32 signed      | $2^{-31}$ sc       | 16 signed    | $2^{-19}$ sc       | 4096x coarser   |
| $\Omega_0$        | 32 signed      | $2^{-31}$ sc       | 24 signed    | $2^{-23}$ sc       | 256x coarser    |
| $\omega$          | 32 signed      | $2^{-31}$ sc       | 24 signed    | $2^{-23}$ sc       | 256x coarser    |
| $M_0$             | 32 signed      | $2^{-31}$ sc       | 24 signed    | $2^{-23}$ sc       | 256x coarser    |
| $\dot{\Omega}$    | 24 signed      | $2^{-43}$ sc/s     | 16 signed    | $2^{-38}$ sc/s     | 32x coarser     |
| $a_{f0}$          | 22 signed      | $2^{-31}$ s        | 11 signed    | $2^{-20}$ s        | 2048x coarser   |
| $a_{f1}$          | 16 signed      | $2^{-43}$ s/s      | 11 signed    | $2^{-38}$ s/s      | 32x coarser     |

The almanac also lacks harmonic corrections ($C_{rs}$, $C_{rc}$,
$C_{us}$, $C_{uc}$, $C_{is}$, $C_{ic}$), the clock drift rate
$a_{f2}$, and the inclination rate $\dot{i}$. Instead, inclination is
stored as an offset from the nominal 54-degree value:

$$\delta i = i_0 - 0.3 \text{ semi-circles}$$

where 0.3 semi-circles = 54 degrees.


## Data Sources: SEM and YUMA Formats

The application supports two almanac formats from public sources:

SEM format (USCG NAVCEN, `current_sem.al3`): angles are natively in
semi-circles, matching the GPS ICD representation directly. This avoids
any radian-to-semi-circle conversion and preserves the original broadcast
precision.

YUMA format (USAF): angles are in radians. The application converts
on read:

$$x_{\text{sc}} = \frac{x_{\text{rad}}}{\pi}$$

For inclination, the YUMA format provides the absolute inclination, so
the delta is computed as:

$$\delta i = \frac{i_{\text{rad}}}{\pi} - 0.3$$


## Almanac Derived from Ephemeris

When no dedicated almanac source is available, the application derives
almanac parameters from the broadcast ephemeris by applying the coarser
almanac scale factors. This is a lossy truncation: the ephemeris
double-precision values are quantized to the almanac bit-widths at
injection time. The orbit prediction accuracy degrades from
sub-meter (ephemeris) to the kilometer level (almanac), but remains
sufficient for satellite visibility prediction and coarse Doppler
estimation.


# Klobuchar Ionospheric Delay Model

## Physical Basis

The GPS L1 signal (1575.42 MHz, IS-GPS-200 [1] §3.3.1.1) experiences a group delay as it traverses
the ionosphere, proportional to the Total Electron Content (TEC) along the
signal path:

$$\Delta\rho_{\text{iono}} = \frac{40.3 \cdot \text{TEC}}{f^2} \quad \text{(meters)}$$

where TEC is in electrons/m$^2$ and $f$ is the carrier frequency in Hz
(Misra & Enge [8] eq. 7.19).
The equivalent time delay is $\Delta t = \Delta\rho / c$.
For example, at 10 TECU ($10^{17}$ el/m$^2$) the L1 range delay is
$40.3 \times 10^{17} / (1575.42 \times 10^{6})^2 \approx 1.62$ m
($\approx 5.4$ ns).
For single-frequency receivers (like the nRF9151 operating on L1 only),
the TEC must be modeled rather than measured.

The Klobuchar model, broadcast as part of the GPS navigation message,
approximates the ionospheric delay as a half-cosine function during daytime
and a constant floor at night:

$$\Delta t_{\text{iono}} = F \times \begin{cases} 5 \times 10^{-9} + A \cos\!\left(\frac{2\pi(t - 50{,}400)}{P}\right) & \text{if } |x| < \pi/2 \\ 5 \times 10^{-9} & \text{otherwise} \end{cases}$$

where $x = 2\pi(t - 50{,}400)/P$, and:
- $F = 1 + 16(0.53 - E/\pi)^3$ is an obliquity factor (IS-GPS-200 [1]
  §20.3.3.5.2.5; Klobuchar [2]), where $E$ is the satellite elevation
  angle in radians ($F \approx 1.0$ at zenith, $F \approx 3.0$ at
  5-degree elevation)
- $A$ is the amplitude: $A = \sum_{n=0}^{3} \alpha_n \, \phi_m^n$
- $P$ is the period: $P = \sum_{n=0}^{3} \beta_n \, \phi_m^n$
- $\phi_m$ is the geomagnetic latitude of the ionospheric pierce point
- $t$ is the local time at the pierce point in seconds
- The 5 ns floor represents the minimum nighttime delay (IS-GPS-200 [1]
  §20.3.3.5.2.5; Klobuchar [2])


## Coefficient Encoding

The eight Klobuchar coefficients ($\alpha_0$ through $\alpha_3$,
$\beta_0$ through $\beta_3$) are broadcast in the GPS navigation message
and extracted from the RINEX header (labels `GPSA` and `GPSB` in the
`IONOSPHERIC CORR` records). The IS-GPS-200 [1] scale factors (Table
20-X) for modem injection are:

| Coefficient| Scale factor | Storage | Typical value    |
|------------|:-------------|:--------|:-----------------|
| $\alpha_0$ | $2^{-30}$    | int8    | $\sim 10^{-8}$ s |
| $\alpha_1$ | $2^{-27}$    | int8    | $\sim 10^{-8}$ s/sc |
| $\alpha_2$ | $2^{-24}$    | int8    | $\sim 10^{-7}$ s/sc$^2$ |
| $\alpha_3$ | $2^{-24}$    | int8    | $\sim 10^{-7}$ s/sc$^3$ |
| $\beta_0$  | $2^{11}$     | int8    | $\sim 10^{5}$ s |
| $\beta_1$  | $2^{14}$     | int8    | $\sim 10^{5}$ s/sc |
| $\beta_2$  | $2^{16}$     | int8    | $\sim 10^{5}$ s/sc$^2$ |
| $\beta_3$  | $2^{16}$     | int8    | $\sim 10^{5}$ s/sc$^3$ |


## Model Accuracy and Limitations

The Klobuchar model corrects approximately **50% RMS** of the ionospheric
delay (Klobuchar [2], Table I). The residual typically ranges from 2 to
10 meters equivalent range error (Misra & Enge [8] Section 7.3.1)
depending on:

- **Solar activity**: during solar maximum, TEC can be several times
  higher than solar minimum (Misra & Enge [8] Section 7.2), and the
  Klobuchar model underestimates the peak
- **Time of day**: the half-cosine model poorly captures post-sunset
  irregularities and pre-dawn enhancement
- **Latitude**: equatorial anomaly regions see delay gradients that exceed
  the polynomial's modeling capability
- **Geomagnetic storms**: sudden TEC enhancements are not reflected in the
  slowly-updated broadcast coefficients

For TTFF purposes, the Klobuchar model's primary contribution is not
positioning accuracy but rather improving the pseudorange estimate used
for code-phase prediction. Even a 50% correction reduces the code-phase
search window compared to having no ionospheric model at all.


# UTC Parameters and Time System Conversions

## GPS Time vs. UTC

GPS time is a continuous time scale that does not incorporate leap
seconds, diverging from UTC by an integer number of seconds (currently
18 s as of 2026, per IERS Bulletin C). The relationship is:

$$t_{\text{UTC}} = t_{\text{GPS}} - \Delta t_{\text{LS}} + \delta t_{\text{UTC}}$$

where $\Delta t_{\text{LS}}$ is the integer leap second count and
$\delta t_{\text{UTC}}$ is the fractional correction:

$$\delta t_{\text{UTC}} = A_0 + A_1 (t - t_{ot} + 604{,}800(WN - WN_t))$$

The parameters $A_0$ (UTC bias, scale $2^{-30}$ s), $A_1$ (UTC drift,
scale $2^{-50}$ s/s), $t_{ot}$ (reference time, scale $2^{12}$ s), and
$WN_t$ (reference week, mod 256) are extracted from the RINEX header
(`GPUT` label in `TIME SYSTEM CORR`).


## Time Representation in the Application

The application maintains time in two domains:

- **Unix time** (`timestamp` field): seconds since January 1, 1970
  00:00:00 UTC, used for data generation timestamps and expiry checks.
- **GPS time**: computed as
  $t_{\text{GPS}} = t_{\text{Unix}} - 315{,}964{,}800 + 18$,
  where 315,964,800 is the Unix timestamp of the GPS epoch (January 6,
  1980) and 18 is the current leap second offset.

For modem injection, GPS time is decomposed into:
- `date_day`: integer GPS days since epoch ($t_{\text{GPS}} / 86{,}400$)
- `time_full_s`: seconds into the current GPS day ($t_{\text{GPS}} \bmod 86{,}400$)


# Reference Location Model

## Location Encoding

The receiver's approximate position is provided to narrow the visible
satellite set and improve Doppler prediction. The application encodes
WGS-84 geodetic coordinates as scaled integers (3GPP TS 23.032 [4] §5):

$$N_{\text{lat}} = \text{round}\!\left(\phi \cdot \frac{2^{23}}{90}\right), \qquad N_{\text{lon}} = \text{round}\!\left(\lambda \cdot \frac{2^{24}}{360}\right)$$

where $\phi$ is latitude in degrees and $\lambda$ is longitude. The
latitude resolution is $90 / 2^{23} \approx 1.073 \times 10^{-5}$ degrees,
corresponding to $1.073 \times 10^{-5} \times 111{,}320 \approx 1.19$ m at
the equator ($111{,}320$ m/degree is the meridional arc length). The
longitude resolution is $360 / 2^{24} \approx 2.146 \times 10^{-5}$
degrees, corresponding to $2.146 \times 10^{-5} \times 111{,}320 \approx 2.39$ m at the equator (finer at higher latitudes due to meridian
convergence).


## Uncertainty Model

The location uncertainty is encoded using a logarithmic model defined in
3GPP TS 23.032 [4] §6.2:

$$r = 10 \cdot (1.1^K - 1) \quad \text{meters}$$

The application uses $K = 18$, yielding:

$$r = 10 \cdot (1.1^{18} - 1) = 10 \cdot (5.5599 - 1) = 45.6 \text{ m}$$

In practice, this represents a city-level position hint. The confidence
level is set to 68% (approximately 1$\sigma$), meaning the true position
falls within the uncertainty ellipse with 68% probability.

For a reference location error of $\delta r$ meters, the worst-case
impact on Doppler prediction is (when the position error maximally
perturbs the line-of-sight direction in the velocity plane):

$$\delta f_d \leq \frac{v_{\text{sat}} \cdot \delta r}{R_{\text{sat}} \cdot \lambda_{L1}}$$

where $v_{\text{sat}} = \sqrt{\mu/R} \approx 3{,}874$ m/s is the orbital
velocity (derived in the Acquisition section),
$R_{\text{sat}} \approx 26{,}560$ km is the orbital radius (IS-GPS-200
[1], typical for GPS MEO), and
$\lambda_{L1} = c/f_{L1} = 0.1903$ m. For $\delta r = 50$ km:

$$\delta f_d \approx \frac{3874 \times 50{,}000}{26{,}560{,}000 \times 0.1903} \approx 38 \text{ Hz}$$

This is well within a single 500 Hz frequency bin, confirming that even a
coarse location hint provides significant acquisition benefit.


# IS-GPS-200 Quantization and Scaling

## The Conversion Pipeline

The application maintains all orbital parameters in IEEE 754
double-precision floating point until the final injection step. The
pipeline is:

```
RINEX text    double-precision    IS-GPS-200 scaled integers
(ASCII) -----> (gps_assist.h) ----------> (nrf_modem structs)
         parse               convert_ephemeris()
```

The conversion follows the general pattern:

$$n_{\text{ICD}} = \text{round}\!\left(\frac{x_{\text{double}}}{2^{-s}}\right)$$

where $s$ is the scale factor exponent from IS-GPS-200. For angular
parameters stored in radians, a prior conversion to semi-circles is
applied:

$$x_{\text{sc}} = \frac{x_{\text{rad}}}{\pi}$$


## Complete Scale Factor Table

The following table lists every parameter converted by the application,
with its IS-GPS-200 [1] scale factor, storage type, and the resulting
quantization step (Least Significant Bit value):

### Ephemeris Parameters (IS-GPS-200 [1] Table 20-III)

| Parameter        | ICD Scale   | LSB Value              | Storage     | Unit after scaling |
|------------------|:-----------:|:-----------------------|:------------|:-------------------|
| $a_{f0}$         | $2^{-31}$   | 4.657 x 10$^{-10}$ s | int32 (22b) | seconds            |
| $a_{f1}$         | $2^{-43}$   | 1.137 x 10$^{-13}$ s/s | int16      | seconds/second     |
| $a_{f2}$         | $2^{-55}$   | 2.776 x 10$^{-17}$ s/s$^2$ | int8 | seconds/second$^2$ |
| $T_{GD}$         | $2^{-31}$   | 4.657 x 10$^{-10}$ s | int8        | seconds            |
| $t_{oc}$, $t_{oe}$ | $2^{4}$ | 16 s                   | uint16      | seconds            |
| $\omega$         | $2^{-31}$   | 4.657 x 10$^{-10}$ sc | int32       | semi-circles       |
| $\Delta n$       | $2^{-43}$   | 1.137 x 10$^{-13}$ sc/s | int16    | semi-circles/s     |
| $M_0$            | $2^{-31}$   | 4.657 x 10$^{-10}$ sc | int32       | semi-circles       |
| $\dot{\Omega}$   | $2^{-43}$   | 1.137 x 10$^{-13}$ sc/s | int32 (24b) | semi-circles/s |
| $e$              | $2^{-33}$   | 1.164 x 10$^{-10}$    | uint32      | dimensionless      |
| $\dot{i}$        | $2^{-43}$   | 1.137 x 10$^{-13}$ sc/s | int16 (14b) | semi-circles/s |
| $\sqrt{A}$       | $2^{-19}$   | 1.907 x 10$^{-6}$ m$^{1/2}$ | uint32 | meters$^{1/2}$ |
| $i_0$            | $2^{-31}$   | 4.657 x 10$^{-10}$ sc | int32       | semi-circles       |
| $\Omega_0$       | $2^{-31}$   | 4.657 x 10$^{-10}$ sc | int32       | semi-circles       |
| $C_{rs}$, $C_{rc}$ | $2^{-5}$ | 0.03125 m              | int16       | meters             |
| $C_{us}$, $C_{uc}$ | $2^{-29}$ | 1.863 x 10$^{-9}$ rad | int16     | radians            |
| $C_{is}$, $C_{ic}$ | $2^{-29}$ | 1.863 x 10$^{-9}$ rad | int16     | radians            |

### Almanac Parameters (IS-GPS-200 [1] Table 20-VI)

| Parameter        | ICD Scale   | LSB Value              | Storage     | Unit after scaling |
|------------------|:-----------:|:-----------------------|:------------|:-------------------|
| $e$              | $2^{-21}$   | 4.768 x 10$^{-7}$     | uint16      | dimensionless      |
| $t_{oa}$         | $2^{12}$    | 4096 s                 | uint8       | seconds            |
| $\delta i$       | $2^{-19}$   | 1.907 x 10$^{-6}$ sc | int16       | semi-circles       |
| $\dot{\Omega}$   | $2^{-38}$   | 3.638 x 10$^{-12}$ sc/s | int16    | semi-circles/s     |
| $\sqrt{A}$       | $2^{-11}$   | 4.883 x 10$^{-4}$ m$^{1/2}$ | uint32 (24b) | meters$^{1/2}$ |
| $\Omega_0$       | $2^{-23}$   | 1.192 x 10$^{-7}$ sc | int32 (24b) | semi-circles       |
| $\omega$         | $2^{-23}$   | 1.192 x 10$^{-7}$ sc | int32 (24b) | semi-circles       |
| $M_0$            | $2^{-23}$   | 1.192 x 10$^{-7}$ sc | int32 (24b) | semi-circles       |
| $a_{f0}$         | $2^{-20}$   | 9.537 x 10$^{-7}$ s   | int16 (11b) | seconds            |
| $a_{f1}$         | $2^{-38}$   | 3.638 x 10$^{-12}$ s/s | int16 (11b) | seconds/second   |


## Quantization Error Analysis

The maximum quantization error for each parameter is half the LSB. For
the most critical parameter affecting position --- the semi-major axis
$\sqrt{A}$ --- the quantization error is:

$$\delta(\sqrt{A}) = \frac{1}{2} \times 2^{-19} = 9.54 \times 10^{-7} \text{ m}^{1/2}$$

For a typical GPS semi-major axis of $A \approx 26{,}560$ km
($\sqrt{A} \approx 5153.64$ m$^{1/2}$), the resulting semi-major axis
error is:

$$\delta A = 2\sqrt{A}\,\delta(\sqrt{A}) \approx 2 \times 5153.64 \times 9.54 \times 10^{-7} \approx 0.0098 \text{ m}$$

This sub-centimeter quantization noise is negligible compared to other
error sources. Similarly, the angular quantization at $2^{-31}$
semi-circles (ephemeris) produces:

$$\delta\theta = \frac{\pi}{2} \times 2^{-31} \approx 7.3 \times 10^{-10} \text{ rad}$$

At orbital radius, this corresponds to:

$$\delta_{\text{pos}} = R \cdot \delta\theta = 26{,}560{,}000 \times 7.3 \times 10^{-10} \approx 0.019 \text{ m}$$

For the almanac, the coarser quantization at $2^{-23}$ semi-circles
yields:

$$\delta_{\text{pos,alm}} = R \cdot \frac{\pi}{2} \times 2^{-23} \approx 26{,}560{,}000 \times 1.87 \times 10^{-7} \approx 4.97 \text{ m}$$

This 5 m quantization error is negligible compared to the kilometer-level
orbit prediction errors inherent in the almanac's simplified model (which
lacks harmonic corrections and degrades with extrapolation over
days to weeks). Quantization is therefore not the limiting factor for
almanac accuracy.


# TTFF Estimation Model

## Definitions

We define four operational scenarios, each characterized by the assistance
data available to the receiver at startup:

| Scenario     | Ephemeris | Almanac | Iono | UTC | Ref. Time | Ref. Location |
|:-------------|:---------:|:-------:|:----:|:---:|:---------:|:-------------:|
| Cold start   | ---       | ---     | --- | --- | ---       | ---           |
| Warm start   | ---       | Yes     | --- | --- | Approximate | ---         |
| Hot start    | Yes       | (Yes)   | --- | --- | Yes       | ---           |
| Full A-GPS   | Yes       | (Yes)   | Yes | Yes | Yes       | Yes           |

"(Yes)" indicates the almanac is implicitly available when ephemeris is
present (derived by truncation).


## Search Space Quantification

### Cold Start: No Assistance

With no prior knowledge, the receiver must:
1. Search all 32 PRN codes
2. Search the full $\pm$ 10 kHz Doppler range (40 bins at 500 Hz spacing)
3. Search all 2046 half-chip code-phase bins
4. Decode the full navigation message to extract ephemeris (minimum 18--30
   seconds of bit synchronization + subframe collection)

The navigation message decoding dominates: even with instantaneous signal
acquisition, the receiver must collect subframes 1--3 (ephemeris) at 6
seconds per subframe, plus time to achieve bit synchronization.

$$T_{\text{cold}} \approx T_{\text{acq,cold}} + T_{\text{nav}} \approx 5\text{--}15 + 18\text{--}30 = 23\text{--}45 \text{ s}$$

This 23--45 s estimate is an **idealized open-sky lower bound** that
assumes instantaneous signal acquisition and only ephemeris collection
(subframes 1--3). In a true cold start, the receiver must also collect
almanac data to learn the full constellation status. The GPS navigation
message broadcasts the almanac in subframes 4 and 5 over a 25-page
cycle: $25 \times 30 \text{ s} = 750 \text{ s} = 12.5 \text{ min}$
(IS-GPS-200 [1], Section 20.3.3.5). This almanac collection is the
dominant contributor to cold-start TTFF.

Additional factors inflate real-world TTFF beyond this theoretical
minimum: weak signals (low C/N0) cause bit errors that force subframe
retransmission; partial sky visibility (urban canyons, indoor) reduces
the pool of decodable satellites; and the nRF9151 has fewer parallel
correlator channels than dedicated GNSS chipsets (the exact count is not
publicly documented [5]). The theoretical worst-case TTFF is:

$$T_{\text{cold,max}} = T_{\text{acq}} + T_{\text{almanac}} + T_{\text{ephemeris}} = T_{\text{acq}} + 750 + 18 \text{ s}$$

In environments with marginal signal strength, retransmissions can
double or triple the almanac collection time. Field observations on the
nRF9151 confirm cold-start TTFF in the range of **2 to 10+ minutes**
under varying sky conditions.


### Warm Start: Almanac + Approximate Time

The almanac enables:
- Satellite visibility prediction: from any point on Earth, typically
  8--12 GPS satellites are above a 5-degree elevation mask at any time
  (Misra & Enge [8] Section 3.4). The almanac lets the receiver predict
  this visible set, eliminating the ~20 non-visible PRNs from the search.
- Coarse Doppler prediction: the orbit-related Doppler error from a
  2 km almanac position uncertainty is
  $\sigma_{f_d} = v_{\text{sat}} \cdot \sigma_r / (R \cdot \lambda_{L1}) = 3874 \times 2000 / (26{,}560{,}000 \times 0.1903) \approx 1.5$ Hz, which is negligible (well within one 500 Hz bin). The dominant
  Doppler uncertainty in a warm start comes from the receiver's TCXO.
  A warm start implies the TCXO offset was calibrated at the last fix;
  the residual drift since calibration is typically a few hundred Hz,
  reducing the search from $\pm$ 10 kHz to approximately $\pm$ 1.5 kHz,
  or about 6 frequency bins instead of 40

However, the receiver still needs to collect fresh ephemeris from the
navigation message. The search space per satellite is:

$$N_{\text{warm}} = 2046 \times 6 = 12{,}276 \text{ bins}$$

A single-channel serial search would need $12{,}276 \times 0.001 / 0.5
\approx 25$ s per satellite. However, the nRF9151 searches multiple code
phases and satellites in parallel using hardware correlator channels, so
the effective acquisition time across 8--12 visible SVs is approximately
1--3 s (consistent with Nordic Semiconductor's documented warm-start
performance [5]). The receiver must still collect subframes 1--3 from the
navigation message for fresh ephemeris:

$$T_{\text{warm}} \approx T_{\text{acq,warm}} + T_{\text{nav}} \approx 1\text{--}3 + 18\text{--}30 = 19\text{--}33 \text{ s}$$

The navigation data collection still dominates TTFF even though
acquisition is fast.


### Hot Start: Recent Ephemeris + Time

With valid ephemeris (less than 4 hours old, within the IS-GPS-200 [1]
nominal fit interval) and accurate time:

- **Precise Doppler prediction**: ephemeris orbit accuracy is
  $\sigma_r < 3$ m (GPS SPS Performance Standard [11] Section 3.3.1).
  The orbit-related Doppler prediction error is
  $\sigma_{f_d} = v_{\text{sat}} \cdot \sigma_r / (R \cdot \lambda_{L1}) = 3874 \times 3 / (26{,}560{,}000 \times 0.1903) \approx 0.002$ Hz,
  far below one frequency bin. The modem calibrates its TCXO offset
  during previous fixes (AFC), retaining frequency lock between fixes.
  The residual frequency uncertainty after AFC calibration is small
  compared to one 500 Hz bin, so the frequency search is reduced to
  1--2 bins.
- **No navigation data decoding needed**: the injected ephemeris
  eliminates the 18+ second subframe collection time. This is the single
  largest TTFF reduction.
- **Code-phase search unchanged**: without reference location (see
  scenario definition table), the receiver cannot predict the
  satellite-receiver range. Code phase repeats every 1 ms
  ($\approx$ 300 km), so only sub-microsecond time knowledge or
  known position (neither available in this scenario) would narrow
  the 2046-bin code-phase search. The full code-phase sweep remains, but at only 1--2 frequency
  bins per code bin the total search is much smaller.

$$N_{\text{hot}} \approx 2046 \times 2 = 4{,}092 \text{ bins}$$

With the nRF9151's parallel correlator channels searching multiple code
phases simultaneously, the acquisition time is:

$$T_{\text{hot}} \approx T_{\text{acq,hot}} + T_{\text{verify}}$$

where $T_{\text{verify}}$ is the time for pseudorange measurements from
$\geq 4$ satellites to converge to a position fix (typically a few
seconds). Nordic Semiconductor's documentation [5] specifies hot-start
TTFF in the range of **5--10 s**.


### Full A-GPS: Ephemeris + Iono + UTC + Time + Location

The addition of reference location and ionospheric corrections to the hot
start scenario provides:
- **Immediate visible set**: knowing the receiver position to within
  the uncertainty radius ($K = 18 \to 45.6$ m, see Reference Location
  section) allows exact prediction of which satellites are above the
  elevation mask, eliminating all non-visible PRNs from the search.
- **Tighter Doppler prediction**: with position known, the line-of-sight
  geometry to each satellite is fully determined. The remaining Doppler
  error comes only from orbit error and receiver clock drift, both of
  which are well-constrained with fresh ephemeris. The residual is below
  one 500 Hz frequency bin (as derived in the hot-start Doppler analysis
  above), so the search collapses to a single frequency bin.
- **Pseudorange correction**: the Klobuchar ionospheric model removes
  approximately 50% of the ionospheric delay (Klobuchar [2]), equivalent
  to 2--10 m of range error reduction depending on conditions. This
  accelerates position solution convergence.

Nordic Semiconductor's product documentation [5] specifies A-GPS TTFF
of **1--3 s** under these conditions.


## TTFF Summary

| Scenario   | Acq. bins/SV | SVs to search | Nav decode     | Estimated TTFF     | Source      |
|:-----------|:------------:|:-------------:|:--------------:|:------------------:|:------------|
| Cold start | 81,840       | 32            | Full (12+ min) | 2 -- 10+ min       | Field obs.  |
| Warm start | 12,276       | 8 -- 12       | Eph only (18 s)| 19 -- 33 s         | Derived     |
| Hot start  | 4,092        | 8 -- 12       | No             | 5 -- 10 s          | [5]         |
| Full A-GPS | 2,046        | 8 -- 12       | No             | 1 -- 3 s           | [5]         |


## QZSS Augmentation Effect

The Quasi-Zenith Satellite System (QZSS) provides GPS-compatible ranging
signals from quasi-zenith orbits optimized for high-elevation visibility
in the Asia-Pacific region. The application supports up to 10 QZSS
satellites (PRN 193--202).

In the QZSS service area, the additional ranging sources provide:
- 1--3 additional satellites always at high elevation, improving geometry
  (lower GDOP) and accelerating position convergence
- QZSS ephemeris uses the same Keplerian model and ICD scale factors as
  GPS, so the application processes them identically

Outside the QZSS service area, these satellites are below the horizon and
contribute no benefit. Within the service area, the additional
high-elevation satellites improve the Geometric Dilution of Precision
(GDOP), which accelerates position solution convergence. The magnitude
of the TTFF improvement depends on the specific satellite geometry at the
time of fix and is not quantified here.


## TTFF as a Function of Assistance Data Age

The following table estimates the expected TTFF when full A-GPS data
(ephemeris + almanac + ionospheric + UTC + reference time + reference
location) is injected, as a function of the time elapsed since the data
was generated. Assumptions: open-sky conditions, nRF9151 modem.

**Orbit error sources:** the 0--4 h values are from the GPS SPS
Performance Standard [11] (URE $\leq$ 0.8 m within validity). The
4--24 h values are from Warren & Raquet [12] empirical characterization
of broadcast ephemeris extrapolation. Beyond 24 h the orbit knowledge
degrades to almanac-level accuracy, sourced from Kaplan & Hegarty [9]
Section 2.5.

**Orbit-related Doppler error** is derived throughout from:
$\sigma_{f_d} = v_{\text{sat}} \cdot \sigma_r / (R \cdot \lambda_{L1})$
with $v_{\text{sat}} = 3{,}874$ m/s, $R = 26{,}560$ km,
$\lambda_{L1} = 0.1903$ m (see derivation in Acquisition section).
This component is small (< 40 Hz even for 50 km orbit error) because the
orbital radius is large compared to the position error. The practical
frequency search width is dominated by the receiver TCXO residual
uncertainty after AFC calibration, which degrades as time since the last
fix increases.

**TTFF estimates** for the 0--4 h and hot-start regimes are from Nordic
Semiconductor [5]. Warm-start and degraded regimes are derived from the
search-space analysis in this paper plus the 18 s minimum navigation
message collection time.

| Data age     | Orbit error    | Orbit Doppler  | Dominant regime  | Estimated TTFF |
|:-------------|:--------------:|:--------------:|:-----------------|:--------------:|
| 0 -- 1 h     | < 0.8 m        | < 0.001 Hz     | Full A-GPS       | 1 -- 3 s       |
| 1 -- 2 h     | < 0.8 m        | < 0.001 Hz     | Full A-GPS       | 1 -- 3 s       |
| 2 -- 4 h     | 0.8 -- 3 m     | 0.001 -- 0.002 Hz | Full A-GPS    | 2 -- 4 s       |
| 4 -- 6 h     | 3 -- 15 m      | 0.002 -- 0.01 Hz  | Hot start     | 3 -- 6 s       |
| 6 -- 8 h     | 15 -- 40 m     | 0.01 -- 0.03 Hz   | Hot start     | 4 -- 8 s       |
| 8 -- 12 h    | 40 -- 100 m    | 0.03 -- 0.08 Hz   | Hot start     | 5 -- 10 s      |
| 12 -- 18 h   | 100 -- 300 m   | 0.08 -- 0.23 Hz   | Degraded hot  | 6 -- 12 s      |
| 18 -- 24 h   | 300 -- 700 m   | 0.23 -- 0.54 Hz   | Degraded hot  | 8 -- 15 s      |
| 24 -- 48 h   | 0.7 -- 2 km    | 0.5 -- 1.5 Hz     | Warm (+nav)   | 20 -- 40 s     |
| 2 -- 7 days  | 2 -- 5 km      | 1.5 -- 3.8 Hz     | Warm (+nav)   | 25 -- 50 s     |
| 1 -- 2 weeks | 5 -- 10 km     | 3.8 -- 7.7 Hz     | Degraded warm | 30 -- 90 s     |
| 2 -- 4 weeks | 10 -- 50 km    | 7.7 -- 38 Hz      | Near cold start| 1 -- 5 min    |
| > 4 weeks    | > 50 km        | > 38 Hz            | Cold start    | 2 -- 10+ min   |

The orbit Doppler error remains below one 500 Hz frequency bin for all
data ages up to several weeks. This means the orbit prediction error is
never the bottleneck for Doppler search --- the TCXO residual and AFC
state dominate. The primary TTFF impact of stale data comes from:

- **0 -- 4 h (ephemeris valid)**: the data is within its nominal validity
  window. The receiver operates in full A-GPS mode with single-bin Doppler
  search. TTFF is dominated by signal acquisition and position convergence
  rather than search overhead. This is the optimal operating point.

- **4 -- 24 h (ephemeris expired, orbit still predictable)**: the
  ephemeris is past its validity window but orbit prediction errors remain
  moderate (meters to hundreds of meters). The receiver still skips
  navigation message decoding and uses the stale ephemeris for position
  computation, though position solution accuracy degrades as the orbit
  prediction error grows. TTFF grows gradually from 3 s to 15 s.

- **24 h -- 7 days (ephemeris unusable, almanac effective)**: orbit
  prediction from the ephemeris degrades to kilometer level, comparable to
  almanac accuracy. The receiver can still predict satellite visibility,
  but must collect fresh ephemeris from the navigation message (18--30 s).
  The injected almanac (derived from the original ephemeris) remains
  useful for weeks.

- **> 2 weeks (almanac degrading)**: satellite health status may have
  changed and orbit errors grow beyond the constellation visibility
  prediction capability. The receiver approaches cold-start conditions,
  potentially requiring a full almanac collection cycle (12.5 minutes) if
  enough SVs have been repositioned or decommissioned.

The transition from full A-GPS (1--3 s) to effective cold start
(2--13 min) represents a TTFF degradation factor of roughly **100x to
400x**, underscoring the importance of data refresh for deployed systems.
For a device that fixes position once per hour, data aged 1--4 hours
provides near-optimal TTFF; for a device that fixes once per day, the
12--24 h regime still delivers 3--10x improvement over cold start.


# Data Freshness and Error Growth

## Ephemeris Validity and Orbit Error Growth

GPS broadcast ephemeris is nominally valid for 4 hours centered on the
time of ephemeris ($t_{oe}$). Beyond this window, orbit prediction errors
grow due to unmodeled perturbations (higher-order gravitational harmonics,
solar radiation pressure, atmospheric drag residuals).

Within the validity window, the broadcast ephemeris achieves a user range
error (URE) better than 0.8 m (1$\sigma$) per the GPS SPS Performance
Standard [11] (Section 3.3.1). Beyond the validity window, orbit
prediction errors grow approximately quadratically due to unmodeled
perturbations. Warren & Raquet [12] characterize this growth empirically;
the values in the table below are based on their results:

$$\sigma_{\text{orbit}}(\Delta t) \approx \sigma_0 + k \cdot \Delta t^2$$

where $\sigma_0 \leq 0.8$ m (URE within validity) and the growth rate
$k$ depends on the specific perturbation regime. The resulting Doppler
prediction error is (using the geometry from the Doppler derivation in
the Acquisition section):

$$\sigma_{f_d}(t) \approx \frac{\sigma_{\text{orbit}}(t)}{R \cdot \lambda_{L1}} \cdot v_{\text{sat}}$$

Orbit errors within validity (0--4 h) are from the GPS SPS Performance
Standard [11]; beyond validity, values are from Warren & Raquet [12].
Doppler errors are derived using
$\sigma_{f_d} = v_{\text{sat}} \cdot \sigma_r / (R \cdot \lambda_{L1})$:

| Ephemeris age | Orbit error (1$\sigma$) | Orbit Doppler error | Impact on TTFF |
|:-------------:|:-----------------------:|:-------------------:|:---------------|
| 0 -- 2 h      | < 0.8 m                 | < 0.001 Hz          | None           |
| 2 -- 4 h      | 0.8 -- 3 m              | 0.001 -- 0.002 Hz   | Negligible     |
| 4 -- 6 h      | 3 -- 15 m               | 0.002 -- 0.01 Hz    | Negligible     |
| 6 -- 12 h     | 15 -- 100 m             | 0.01 -- 0.08 Hz     | Negligible     |
| 12 -- 24 h    | 100 -- 700 m            | 0.08 -- 0.54 Hz     | Negligible     |
| > 24 h        | > 700 m                 | > 0.54 Hz            | Degrades to warm start |

Note: the orbit-related Doppler error remains far below one 500 Hz
frequency bin even at 24 h. The primary TTFF impact of stale ephemeris is
not Doppler search expansion but degraded position solution convergence
from growing orbit prediction errors.


## Clock Correction Decay

The satellite clock polynomial is referenced to $t_{oc}$. The broadcast
coefficients $a_{f0}$, $a_{f1}$, $a_{f2}$ are applied as corrections,
so the residual clock error after correction comes from two sources:

1. **Oscillator instability** beyond what the polynomial captures:
   characterised by the Allan deviation $\sigma_y$ at the extrapolation
   interval $\Delta t$, contributing a time error
   $\sigma_x \approx \sigma_y \cdot \Delta t$.
2. **Polynomial truncation**: higher-order frequency variations not
   modeled by the quadratic, plus uncertainties in the estimated
   coefficients themselves, which grow with extrapolation distance.

The combined residual clock prediction error is therefore:

$$\sigma_{\text{clock}}(\Delta t) \approx \sigma_y \cdot \Delta t + \sigma_{\text{trunc}}(\Delta t)$$

where $\sigma_y \approx 10^{-12}$ s/s at $\tau = 1$ day for Rb
oscillators (Misra & Enge [8] Section 9.2) and
$\sigma_{\text{trunc}}$ represents the polynomial truncation and
coefficient uncertainty contributions. After 24 hours, the oscillator
instability term alone contributes
$10^{-12} \times 86{,}400 \times c \approx 25.9$ m of pseudorange
error, plus the truncation contribution. The combined effect inflates
TTFF by degrading position solution convergence.


## Almanac Validity

The almanac changes slowly because GPS orbits are highly stable. The
primary failure mode is not orbit error but **satellite health status
changes** --- new satellites entering service, existing satellites marked
unhealthy for maintenance, or repositioning maneuvers that render the
almanac orbit invalid for affected PRNs.

Almanac orbit prediction accuracy depends on the age of the almanac
dataset. The Doppler error column is derived from the orbit error using
$\sigma_{f_d} = v_{\text{sat}} \cdot \sigma_r / (R \cdot \lambda_{L1})$ with $v_{\text{sat}} = 3{,}874$ m/s, $R = 26{,}560$ km,
$\lambda_{L1} = 0.1903$ m (see Acquisition section derivations). Orbit
error magnitudes are from Kaplan & Hegarty [9] Section 2.5:

| Almanac age  | Orbit error | Orbit Doppler    | Usability          |
|:------------:|:-----------:|:----------------:|:-------------------|
| 0 -- 1 day   | < 1 km      | < 0.8 Hz         | Full warm start    |
| 1 -- 7 days  | 1 -- 3 km   | 0.8 -- 2.3 Hz    | Useful             |
| 1 -- 4 weeks | 3 -- 10 km  | 2.3 -- 7.7 Hz    | Visibility only    |
| > 4 weeks    | > 10 km     | > 7.7 Hz          | Near cold start    |

All orbit-related Doppler errors remain within one 500 Hz frequency bin
even for 4-week-old almanacs. The primary degradation with almanac age is
the loss of accurate satellite visibility prediction and health status,
not Doppler search expansion.


## Ionospheric Model Staleness

The Klobuchar coefficients are updated by the GPS Control Segment
approximately every few days. However, ionospheric conditions change on
diurnal (hours), seasonal (months), and solar cycle (11-year) timescales.
Using stale ionospheric coefficients primarily affects **position
accuracy** rather than TTFF, since the model's contribution to code-phase
prediction is secondary. The additional range error from stale
coefficients depends on the ionospheric variability during the staleness
interval and is bounded by the Klobuchar model's inherent 50% residual
(Klobuchar [2]).


## BKG BRDC Data Availability

The application downloads combined BRDC navigation files from the
Bundesamt fur Kartographie und Geodasie (BKG) IGS archive. These files
are produced daily and contain all constellations (GPS, GLONASS, Galileo,
BeiDou, QZSS).

Operational experience has shown that:
- Files for the current day are built incrementally and may temporarily
  lack GPS records while containing data from other constellations
- A day-2 fallback strategy (try yesterday, then day-before-yesterday) is
  necessary for production reliability
- The file size serves as a useful heuristic: files under 100 KB are
  likely incomplete (a full combined BRDC file is typically 1--2 MB)

The TTFF impact of using day-old versus fresh data depends on the
ephemeris age analysis above: a 24--48 hour old ephemeris degrades TTFF
by an estimated 1--3 seconds compared to a fresh ephemeris, but remains
vastly superior to a cold start.


# Implementation Architecture

## Data Pipeline

The application implements a four-stage pipeline:

```
                                              nrf_modem_gnss_
 BKG IGS         gps_assist_data              agnss_write()
 BRDC ---------> (doubles) ---------> ICD integers ---------> modem
  .rnx.gz parse              convert                inject

 NAVCEN ---------> gps_almanac
 SEM/YUMA   parse   (semi-circles)
```

**Stage 1 --- Download:** HTTP GET of the gzipped RINEX navigation file
from BKG IGS, with size validation (> 1 KB).

**Stage 2 --- Parse:** Streaming decompression via zlib (`gzgets`), line-
by-line RINEX v3/v4 parsing. Header records yield ionospheric, UTC, and
leap second parameters. Data records are dispatched by constellation
identifier (`G` for GPS, `J` for QZSS, others skipped). Per PRN, only
the most recent ephemeris (highest $t_{oe}$) is retained.

**Stage 3 --- Code generation:** The `codegen_write()` function emits a C
source file containing a `const struct gps_assist_data` initializer with
all values in double-precision format (12 significant digits via `%.12e`).
This file is compiled into the Zephyr firmware image.

**Stage 4 --- Modem injection:** At runtime, `gps_assist_inject()` calls
the per-type conversion functions (`convert_ephemeris`,
`convert_almanac`, `convert_iono`, `convert_utc`, `convert_system_time`,
`convert_location`), then writes each element to the modem via
`nrf_modem_gnss_agnss_write()`.


## Injection Modes

The application supports three injection strategies:

**Unconditional injection** (`gps_assist_inject`): injects all available
data --- ephemerides for all SVs, almanacs (native SEM/YUMA if available,
else derived from ephemeris), ionospheric model, UTC parameters, system
time, and reference location. Used at initial boot.

**Request-driven injection** (`gps_assist_inject_from_request`): the nRF
modem generates an A-GNSS request event specifying which data types and
which PRNs it needs. The application parses the request bitmasks and
injects only the requested elements. This is more efficient and avoids
overwriting data the modem considers still valid.

**Expiry-driven refresh** (`gps_assist_check_expiry`): queries the modem
for data elements whose validity has expired (expiry time = 0), builds a
synthetic request from the expiry report, and re-injects. This enables
periodic refresh without full re-injection.


# Discussion

## Offline A-GPS vs. Real-Time SUPL

The offline approach implemented here pre-compiles assistance data into
the firmware image, eliminating runtime network dependency for A-GPS. The
trade-offs are:

| Aspect              | Offline A-GPS (this system)    | Real-time SUPL          |
|:--------------------|:-------------------------------|:------------------------|
| Network at fix time | Not required                   | Required (LTE data)     |
| Data freshness      | Hours to days old              | Seconds old             |
| TTFF (best case)    | 1 -- 3 s [5]                   | 1 -- 2 s [5]           |
| TTFF (stale data)   | 5 -- 15 s (see age table)      | N/A (always fresh)     |
| Power consumption   | Lower (no LTE for A-GPS)       | Higher (LTE + GNSS)    |
| Server dependency   | Build-time only                | Runtime dependency     |

For IoT devices that fix position infrequently (once per hour or less),
offline A-GPS provides an excellent trade-off: the slightly degraded TTFF
from 12--24 hour old data is compensated by the elimination of runtime
network overhead.


## Sensitivity to Data Source Completeness

The cumulative TTFF benefit of each data source is nonlinear. The most
impactful single element is the **broadcast ephemeris**, which eliminates
the dominant cold-start cost: navigation message decoding (almanac +
ephemeris collection). The second most impactful is the **reference
location**, which eliminates searching non-visible satellites. The
ionospheric model and UTC parameters provide marginal TTFF improvement
but contribute to position accuracy.

The following waterfall illustrates the relative contribution of each
data source. The cold-start baseline and final A-GPS TTFF are from
field observations and Nordic documentation [5]; the intermediate
decomposition is estimated from the search-space analysis in this paper
and is approximate:

```
Cold start baseline:                     ~2 min (field obs., open sky)
 + Almanac (skip almanac decode):       ~-75 s  -->  ~45 s
 + Ephemeris (skip nav decode):         ~-30 s  -->  ~15 s
 + Reference location (SV visibility):   ~-5 s  -->  ~10 s
 + Precise Doppler (freq bins):          ~-5 s  -->   ~5 s
 + Iono + UTC (convergence):             ~-2 s  -->   ~3 s
 Final: 1--3 s (Nordic Semiconductor [5])
```


## Worst-Case TTFF Scenarios

Several conditions can degrade TTFF even with full A-GPS data:

- Ephemeris older than 4--6 hours: orbit prediction error degrades
  position solution convergence, adding 1--5 seconds (the orbit-related
  Doppler error remains well below one frequency bin, but the growing
  position error slows pseudorange-based fix convergence)
- Location error > 100 km: visible satellite set may be incorrect,
  causing wasted searches
- Indoor/urban canyon: signal attenuation below receiver sensitivity,
  potentially preventing fix entirely regardless of assistance
- Constellation anomaly: if a BKG BRDC file lacks GPS data
  (as observed for DOY 071/2026), the fallback to stale data adds
  1--3 seconds of TTFF penalty


# Conclusion

This paper has presented the complete mathematical and physical framework
underlying an offline A-GPS data pipeline for the nRF9151 modem. The
system processes broadcast navigation data through four stages ---
download, parse, code generation, and modem injection --- applying
IS-GPS-200 scale factors to convert double-precision orbital parameters
into the integer representations expected by the GNSS hardware.

The TTFF analysis demonstrates that full A-GPS assistance reduces
acquisition time by two orders of magnitude compared to cold start (from
2--13 minutes to 1--3 seconds), with the broadcast ephemeris and
reference location being the two most impactful data elements. The
quantization error introduced by the ICD scaling is negligible (sub-
centimeter for ephemeris, < 5 m for almanac).

For deployed IoT systems, the key operational parameter is **data refresh
interval**: ephemeris should be refreshed at least every 4--6 hours for
optimal TTFF, though data up to 24 hours old still provides substantial
benefit over cold start. The almanac, by contrast, remains useful for
weeks and serves as a valuable fallback when fresh ephemeris is
unavailable.


# References

1. IS-GPS-200N, "NAVSTAR GPS Space Segment/Navigation User Segment
   Interfaces," GPS Directorate, 2022.

2. J. A. Klobuchar, "Ionospheric Time-Delay Algorithm for Single-Frequency
   GPS Users," IEEE Transactions on Aerospace and Electronic Systems,
   vol. AES-23, no. 3, pp. 325--331, 1987.

3. RINEX: The Receiver Independent Exchange Format, Version 3.05, IGS/RTCM,
   2020.

4. 3GPP TS 23.032, "Universal Geographical Area Description (GAD),"
   Release 17, 2023.

5. Nordic Semiconductor, "nRF9160/nRF9151 Product Specification" and
   "nRF Connect SDK: GNSS Interface," 2025.

6. BKG IGS Data Center, "GNSS Data and Products,"
   https://igs.bkg.bund.de.

7. U.S. Coast Guard Navigation Center, "GPS Almanac Information,"
   https://www.navcen.uscg.gov/gps-almanacs.

8. P. Misra and P. Enge, *Global Positioning System: Signals, Measurements,
   and Performance*, 2nd ed., Ganga-Jamuna Press, 2006.

9. E. D. Kaplan and C. J. Hegarty, *Understanding GPS/GNSS: Principles and
   Applications*, 3rd ed., Artech House, 2017.

10. B. W. Parkinson and J. J. Spilker Jr., *Global Positioning System:
    Theory and Applications*, AIAA, 1996.

11. GPS Directorate, "Global Positioning System Standard Positioning
    Service Performance Standard," 5th ed., April 2020.

12. D. L. M. Warren and J. F. Raquet, "Broadcast vs. Precise GPS
    Ephemerides: A Historical Perspective," GPS Solutions, vol. 7,
    no. 3, pp. 151--156, 2003.

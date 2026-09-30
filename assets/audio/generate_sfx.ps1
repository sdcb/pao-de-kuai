param(
    [string]$OutputDir = (Split-Path -Parent $PSCommandPath),
    [int]$SampleRate = 44100,
    # LAME VBR quality (0 best .. 9 smallest). Quiet reverb tails get very few bits this way.
    [int]$VbrQuality = 5,
    [string]$FfmpegPath = "ffmpeg"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# Per-sample DSP runs in C#; PowerShell loops are far too slow for filters and reverb.
# Keep the C# at language version 5 so Windows PowerShell 5.1 can compile it too.
$dspSource = @'
using System;
using System.IO;

namespace PdkSfx
{
    public static class Dsp
    {
        public static int Rate = 44100;
        const double Ln1000 = 6.907755278982137;
        static Random rng = new Random(1);

        public static void Seed(int seed) { rng = new Random(seed); }
        static double Rand() { return rng.NextDouble() * 2.0 - 1.0; }
        static double Rand01() { return rng.NextDouble(); }

        public static double Midi(double note) { return 440.0 * Math.Pow(2.0, (note - 69.0) / 12.0); }
        public static double[] Buffer(double seconds) { return new double[Math.Max(1, (int)Math.Ceiling(seconds * Rate))]; }

        sealed class Svf
        {
            double ic1, ic2;
            // Zavalishin TPT state variable filter; stable under fast cutoff sweeps.
            public double Process(double x, double fc, double q, int mode)
            {
                if (fc > Rate * 0.45) fc = Rate * 0.45;
                if (fc < 10.0) fc = 10.0;
                double g = Math.Tan(Math.PI * fc / Rate);
                double k = 1.0 / q;
                double a1 = 1.0 / (1.0 + g * (g + k));
                double a2 = g * a1;
                double a3 = g * a2;
                double v3 = x - ic2;
                double v1 = a1 * ic1 + a2 * v3;
                double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                if (mode == 0) return v2;
                if (mode == 1) return k * v1;
                return x - k * v1 - v2;
            }
        }

        // Raised-cosine attack followed by exponential decay; decay is the time to fall 60 dB.
        static double Env(double t, double attack, double decay)
        {
            if (t < 0.0) return 0.0;
            double a = 1.0;
            if (attack > 0.0 && t < attack) a = 0.5 - 0.5 * Math.Cos(Math.PI * t / attack);
            double tail = t > attack ? t - attack : 0.0;
            return a * Math.Exp(-Ln1000 * tail / decay);
        }

        static int Span(double attack, double decay) { return (int)Math.Ceiling((attack + decay * 1.4) * Rate); }
        static int Index(double seconds) { return (int)Math.Round(seconds * Rate); }

        public static void Sine(double[] b, double start, double freq, double amp, double attack, double decay)
        {
            if (freq <= 0.0 || freq >= Rate * 0.45) return;
            int s0 = Index(start);
            int n = Span(attack, decay);
            double w = 2.0 * Math.PI * freq / Rate;
            for (int i = 0; i < n; i++)
            {
                int k = s0 + i;
                if (k < 0) continue;
                if (k >= b.Length) break;
                b[k] += amp * Env((double)i / Rate, attack, decay) * Math.Sin(w * i);
            }
        }

        public static void Modal(double[] b, double start, double freq, double amp, double attack, double decay,
                                 double[] ratios, double[] gains, double[] decays)
        {
            for (int p = 0; p < ratios.Length; p++)
                Sine(b, start, freq * ratios[p], amp * gains[p], attack, decay * decays[p]);
        }

        // Filtered noise burst. mode: 0 lowpass, 1 bandpass, 2 highpass.
        // Cutoff sweeps exponentially from f0 to f1 over attack + decay; brown 0..1 darkens the source.
        public static void Noise(double[] b, double start, double attack, double decay, double amp,
                                 int mode, double f0, double f1, double q, double brown)
        {
            int s0 = Index(start);
            int n = Span(attack, decay);
            double sweepLen = (attack + decay) * Rate;
            double a = 1.0 - 0.97 * brown;
            double norm = Math.Sqrt((2.0 - a) / a);
            double lp = 0.0;
            Svf f = new Svf();
            for (int i = 0; i < n; i++)
            {
                int k = s0 + i;
                if (k >= b.Length) break;
                double u = Math.Min(1.0, i / sweepLen);
                double fc = f0 * Math.Pow(f1 / f0, u);
                lp += a * (Rand() - lp);
                double y = f.Process(lp * norm, fc, q, mode);
                if (k >= 0) b[k] += amp * Env((double)i / Rate, attack, decay) * y;
            }
        }

        // Sine with an exponential pitch glide from f0 toward f1; used for thumps, drops and bubbles.
        public static void Thump(double[] b, double start, double f0, double f1, double sweep, double amp, double attack, double decay)
        {
            int s0 = Index(start);
            int n = Span(attack, decay);
            double phase = 0.0;
            for (int i = 0; i < n; i++)
            {
                int k = s0 + i;
                if (k >= b.Length) break;
                double t = (double)i / Rate;
                double f = f1 + (f0 - f1) * Math.Exp(-t / sweep);
                phase += 2.0 * Math.PI * f / Rate;
                if (k >= 0) b[k] += amp * Env(t, attack, decay) * Math.Sin(phase);
            }
        }

        // Celesta-like struck metal bar with a slightly detuned twin for slow shimmer.
        public static void Glass(double[] b, double start, double freq, double amp, double decay)
        {
            Modal(b, start, freq, amp, 0.0015, decay,
                new double[] { 1.0, 1.0009, 2.0, 2.756, 5.404 },
                new double[] { 0.72, 0.28, 0.10, 0.15, 0.05 },
                new double[] { 1.0, 1.0, 0.55, 0.32, 0.16 });
            Noise(b, start, 0.0005, 0.006, amp * 0.05, 2, 6000.0, 6000.0, 0.7, 0.0);
        }

        // Marimba-like wooden bar; hardness 0..1 brightens the overtones and the mallet strike.
        public static void Mallet(double[] b, double start, double freq, double amp, double decay, double hardness)
        {
            Modal(b, start, freq, amp, 0.001, decay,
                new double[] { 1.0, 3.99, 9.2 },
                new double[] { 1.0, 0.30 * hardness, 0.10 * hardness },
                new double[] { 1.0, 0.30, 0.12 });
            Noise(b, start, 0.0003, 0.008, amp * 0.12 * hardness, 1, freq * 3.0, freq * 1.5, 0.8, 0.0);
        }

        // Two-operator FM electric piano: warm body, soft bell-like attack, chorused second voice.
        public static void EPiano(double[] b, double start, double freq, double amp, double decay, double hardness)
        {
            int s0 = Index(start);
            int n = Span(0.002, decay);
            double w1 = 2.0 * Math.PI * freq / Rate;
            double w2 = w1 * 1.0017;
            double wt = w1 * 7.0;
            bool tine = freq * 7.0 < Rate * 0.45;
            for (int i = 0; i < n; i++)
            {
                int k = s0 + i;
                if (k >= b.Length) break;
                if (k < 0) continue;
                double t = (double)i / Rate;
                double index = hardness * 1.6 * Math.Exp(-Ln1000 * t / (decay * 0.18)) + 0.12;
                double y = 0.65 * Math.Sin(w1 * i + index * Math.Sin(w1 * i))
                         + 0.35 * Math.Sin(w2 * i + index * 0.8 * Math.Sin(w2 * i));
                if (tine) y += 0.10 * hardness * Math.Sin(wt * i) * Math.Exp(-Ln1000 * t / 0.06);
                b[k] += amp * Env(t, 0.002, decay) * y;
            }
        }

        // Karplus-Strong plucked string with allpass fine tuning (guzheng / harp flavour).
        public static void Pluck(double[] b, double start, double freq, double amp, double decay, double brightness)
        {
            int s0 = Index(start);
            double period = Rate / freq;
            int len = (int)Math.Floor(period - 0.6);
            if (len < 2) return;
            double frac = period - 0.5 - len;
            double c = (1.0 - frac) / (1.0 + frac);
            double loss = Math.Pow(10.0, -3.0 * period / (decay * Rate)) / Math.Cos(Math.PI * freq / Rate);
            if (loss > 0.99999) loss = 0.99999;

            double[] line = new double[len];
            double lp = 0.0, mean = 0.0;
            for (int i = 0; i < len; i++)
            {
                lp += brightness * (Rand() - lp);
                line[i] = lp;
                mean += lp;
            }
            mean /= len;
            for (int i = 0; i < len; i++) line[i] -= mean;

            int n = (int)Math.Ceiling(decay * 1.4 * Rate);
            int pos = 0;
            double prev = 0.0, apx = 0.0, apy = 0.0;
            for (int i = 0; i < n; i++)
            {
                int k = s0 + i;
                if (k >= b.Length) break;
                double x = line[pos];
                double avg = 0.5 * (x + prev);
                prev = x;
                double ap = c * avg + apx - c * apy;
                apx = avg;
                apy = ap;
                line[pos] = ap * loss;
                pos = (pos + 1) % len;
                double attack = i < 40 ? i / 40.0 : 1.0;
                if (k >= 0) b[k] += amp * attack * x * 2.0;
            }
        }

        // Inharmonic gong with beating partial pairs.
        public static void Gong(double[] b, double start, double freq, double amp, double decay)
        {
            Modal(b, start, freq, amp, 0.012, decay,
                new double[] { 1.0, 1.003, 1.52, 1.527, 2.03, 2.61, 2.62, 3.24, 4.13, 5.2 },
                new double[] { 0.80, 0.60, 0.55, 0.40, 0.45, 0.35, 0.25, 0.25, 0.15, 0.08 },
                new double[] { 1.0, 0.95, 0.80, 0.80, 0.70, 0.55, 0.55, 0.45, 0.35, 0.25 });
            Noise(b, start, 0.001, 0.05, amp * 0.25, 0, 3000.0, 800.0, 0.7, 0.3);
        }

        // Knuckle knock on a wooden table.
        public static void Knock(double[] b, double start, double freq, double amp)
        {
            Modal(b, start, freq, amp, 0.0004, 0.06,
                new double[] { 1.0, 2.45, 5.9 },
                new double[] { 1.0, 0.55, 0.20 },
                new double[] { 1.0, 0.55, 0.25 });
            Noise(b, start, 0.0003, 0.008, amp * 0.5, 0, 2800.0, 1200.0, 0.7, 0.3);
            Thump(b, start, freq * 1.25, freq * 0.75, 0.008, amp * 0.5, 0.0004, 0.04);
        }

        // Soft detuned saw pad through a sweeping lowpass.
        public static void Pad(double[] b, double start, double dur, double freq, double amp,
                               double attack, double release, double cut0, double cut1, double q)
        {
            int s0 = Index(start);
            int n = (int)Math.Ceiling(dur * Rate);
            double w1 = 2.0 * Math.PI * freq * 0.9965 / Rate;
            double w2 = 2.0 * Math.PI * freq * 1.0035 / Rate;
            int harmonics = Math.Min(24, (int)Math.Floor(Rate * 0.45 / (freq * 1.0035)));
            Svf f = new Svf();
            for (int i = 0; i < n; i++)
            {
                int k = s0 + i;
                if (k >= b.Length) break;
                double t = (double)i / Rate;
                double env = 1.0;
                if (t < attack) env = 0.5 - 0.5 * Math.Cos(Math.PI * t / attack);
                double tr = dur - t;
                if (tr < release) env *= 0.5 - 0.5 * Math.Cos(Math.PI * Math.Max(0.0, tr) / release);
                double s = 0.0;
                for (int h = 1; h <= harmonics; h++) s += (Math.Sin(w1 * h * i) + Math.Sin(w2 * h * i)) / h;
                double y = f.Process(s * 0.3, cut0 * Math.Pow(cut1 / cut0, t / dur), q, 0);
                if (k >= 0) b[k] += amp * env * y;
            }
        }

        // Tiny random glass notes for sparkle tails.
        public static void Sparkle(double[] b, double start, double span, int count, double[] notes, double amp, double decay)
        {
            for (int i = 0; i < count; i++)
            {
                double t = start + span * Rand01();
                double note = notes[rng.Next(notes.Length)];
                double fade = 1.0 - 0.6 * (t - start) / span;
                Glass(b, t, Midi(note), amp * fade * (0.5 + 0.5 * Rand01()), decay * (0.6 + 0.4 * Rand01()));
            }
        }

        // Cloud of short paper clicks. shape 0 swells and fades (riffle), 1 fades out (debris).
        public static void Scatter(double[] b, double start, double span, int count, double amp,
                                   double fLow, double fHigh, double decayLow, double decayHigh, int shape)
        {
            for (int i = 0; i < count; i++)
            {
                double u = (i + 0.6 * Rand01()) / count;
                double t = start + span * Math.Pow(u, 0.92);
                double env = shape == 0 ? Math.Sqrt(Math.Sin(Math.PI * Math.Min(1.0, u))) : (1.0 - u) * (1.0 - u);
                double level = amp * env * (0.65 + 0.35 * Rand01());
                double f = fLow + (fHigh - fLow) * Rand01();
                double d = decayLow + (decayHigh - decayLow) * Rand01();
                Noise(b, t, 0.0004, d, level, 1, f, f * 0.8, 1.4, 0.0);
                Sine(b, t, f * 0.3, level * 0.12, 0.0003, d * 2.0);
            }
        }

        // Mono Freeverb. size scales the delay lines (smaller = tighter room).
        public static void Reverb(double[] b, double wet, double room, double damp, double size, double predelay)
        {
            int[] combTuning = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            int[] allpassTuning = { 556, 441, 341, 225 };
            int n = b.Length;
            int pd = Index(predelay);
            double feedback = 0.7 + 0.28 * room;
            double d = damp * 0.4;
            double[] acc = new double[n];

            for (int c = 0; c < combTuning.Length; c++)
            {
                int len = Math.Max(8, (int)(combTuning[c] * size * Rate / 44100.0));
                double[] line = new double[len];
                int p = 0;
                double store = 0.0;
                for (int i = 0; i < n; i++)
                {
                    double x = i - pd >= 0 ? b[i - pd] * 0.015 : 0.0;
                    double o = line[p];
                    store = o * (1.0 - d) + store * d;
                    line[p] = x + store * feedback;
                    p = (p + 1) % len;
                    acc[i] += o;
                }
            }
            for (int a = 0; a < allpassTuning.Length; a++)
            {
                int len = Math.Max(4, (int)(allpassTuning[a] * size * Rate / 44100.0));
                double[] line = new double[len];
                int p = 0;
                for (int i = 0; i < n; i++)
                {
                    double o = line[p];
                    line[p] = acc[i] + o * 0.5;
                    acc[i] = o - acc[i];
                    p = (p + 1) % len;
                }
            }
            for (int i = 0; i < n; i++) b[i] += acc[i] * wet * 3.0;
        }

        static double Peak(double[] b)
        {
            double max = 1e-9;
            for (int i = 0; i < b.Length; i++) max = Math.Max(max, Math.Abs(b[i]));
            return max;
        }

        static void Scale(double[] b, double peak)
        {
            double s = peak / Peak(b);
            for (int i = 0; i < b.Length; i++) b[i] *= s;
        }

        // Loudest 50 ms RMS window: tracks perceived level of short cues far better than peak.
        static double MaxWindowRms(double[] b)
        {
            int w = Math.Min(b.Length, Index(0.05));
            double acc = 0.0, best = 0.0;
            for (int i = 0; i < b.Length; i++)
            {
                acc += b[i] * b[i];
                if (i >= w) acc -= b[i - w] * b[i - w];
                if (i >= w - 1) best = Math.Max(best, acc / w);
            }
            return Math.Sqrt(Math.Max(best, 1e-12));
        }

        // Clean up the low end, optionally tame highs and saturate, trim and fade the tail,
        // then match the loudest 50 ms window to loudnessDb without exceeding the peak ceiling.
        public static double[] Finish(double[] b, double highpass, double lowpass, double drive, double loudnessDb, double ceiling)
        {
            Svf hp1 = new Svf(), hp2 = new Svf(), lp = new Svf();
            for (int i = 0; i < b.Length; i++)
            {
                double x = hp2.Process(hp1.Process(b[i], highpass, 0.707, 2), highpass, 0.707, 2);
                if (lowpass > 0.0) x = lp.Process(x, lowpass, 0.707, 0);
                b[i] = x;
            }
            Scale(b, 1.0);
            if (drive > 0.0)
            {
                double norm = Math.Tanh(drive);
                for (int i = 0; i < b.Length; i++) b[i] = Math.Tanh(b[i] * drive) / norm;
            }

            double floor = Math.Pow(10.0, -60.0 / 20.0);
            int last = 0;
            for (int i = b.Length - 1; i >= 0; i--)
            {
                if (Math.Abs(b[i]) > floor) { last = i; break; }
            }
            int len = Math.Min(b.Length, last + Index(0.01));
            double[] o = new double[len];
            Array.Copy(b, o, len);
            int fade = Math.Max(Index(0.012), Math.Min(Index(0.15), len / 5));
            for (int i = 0; i < fade && i < len; i++)
            {
                double u = (double)i / fade;
                o[len - 1 - i] *= u * u * (3.0 - 2.0 * u);
            }

            double gain = Math.Pow(10.0, loudnessDb / 20.0) / MaxWindowRms(o);
            gain = Math.Min(gain, ceiling / Peak(o));
            for (int i = 0; i < len; i++) o[i] *= gain;
            return o;
        }

        public static void WriteWav(string path, double[] b)
        {
            using (FileStream fs = File.Create(path))
            using (BinaryWriter w = new BinaryWriter(fs))
            {
                int dataSize = b.Length * 2;
                w.Write(new char[] { 'R', 'I', 'F', 'F' });
                w.Write(36 + dataSize);
                w.Write(new char[] { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
                w.Write(16);
                w.Write((short)1);
                w.Write((short)1);
                w.Write(Rate);
                w.Write(Rate * 2);
                w.Write((short)2);
                w.Write((short)16);
                w.Write(new char[] { 'd', 'a', 't', 'a' });
                w.Write(dataSize);
                for (int i = 0; i < b.Length; i++)
                {
                    double dither = (Rand01() - Rand01()) / 32768.0;
                    double x = Math.Max(-1.0, Math.Min(1.0, b[i] + dither));
                    w.Write((short)Math.Round(x * 32767.0));
                }
            }
        }
    }
}
'@

if (-not ('PdkSfx.Dsp' -as [type])) {
    Add-Type -TypeDefinition $dspSource -Language CSharp
}
$D = [PdkSfx.Dsp]
$D::Rate = $SampleRate

# Everything tonal sits in D major pentatonic (D E F# A B) so overlapping cues stay consonant.
# MIDI reference: D4=62 E4=64 F#4=66 A4=69 B4=71 D5=74 A5=81 D6=86 A6=93 D7=98.
function N([double]$Midi) { return $D::Midi($Midi) }

function Convert-WavToMp3 {
    param(
        [string]$WavPath,
        [string]$Mp3Path
    )

    # No Xing/Info frame: Media Foundation ignores it and decodes it as ~26 ms of leading silence.
    # src/audio/AudioDecoder.cpp trims the fixed 577-sample LAME delay that this exact setup produces.
    & $FfmpegPath -hide_banner -loglevel error -y `
        -i $WavPath `
        -ac 1 `
        -ar $SampleRate `
        -c:a libmp3lame `
        -q:a $VbrQuality `
        -compression_level 0 `
        -write_xing 0 `
        -map_metadata -1 `
        -id3v2_version 0 `
        $Mp3Path

    if ($LASTEXITCODE -ne 0) {
        throw "ffmpeg failed while converting $WavPath to $Mp3Path. Install ffmpeg or pass -FfmpegPath."
    }
}

function New-Sfx {
    param(
        [string]$Name,
        [double]$Length,
        [scriptblock]$Build,
        [double]$HighPass = 45,
        [double]$LowPass = 0,
        [double]$Drive = 0,
        [double]$Loudness = -16,
        [double]$Ceiling = 0.89,
        [int]$Seed = 1
    )

    $D::Seed($Seed)
    $buffer = $D::Buffer($Length)
    & $Build $buffer
    $final = $D::Finish($buffer, $HighPass, $LowPass, $Drive, $Loudness, $Ceiling)

    $mp3Path = Join-Path $OutputDir $Name
    $tempWav = Join-Path ([System.IO.Path]::GetTempPath()) ([System.IO.Path]::GetRandomFileName() + ".wav")
    try {
        $D::WriteWav($tempWav, $final)
        Convert-WavToMp3 $tempWav $mp3Path
    }
    finally {
        if (Test-Path -LiteralPath $tempWav) {
            Remove-Item -LiteralPath $tempWav -Force
        }
    }
    Write-Host ("Generated {0} ({1:N0} ms)" -f $Name, ($final.Length * 1000.0 / $SampleRate))
}

$defs = @(
    # ---- UI: short, soft, wooden/glass; nothing that chirps. ----
    @{ Name = 'ui_button_click.mp3'; Length = 0.20; Loudness = -18; Drive = 1.4; Build = {
        param($b)
        $D::Noise($b, 0.000, 0.0003, 0.012, 0.55, 1, 3200, 2400, 1.8, 0)
        $D::Mallet($b, 0.000, (N 93), 0.30, 0.045, 0.5)
        $D::Thump($b, 0.000, 420, 260, 0.010, 0.22, 0.0005, 0.020)
        $D::Reverb($b, 0.06, 0.30, 0.6, 0.5, 0.004)
    } },
    @{ Name = 'ui_confirm.mp3'; Length = 0.60; Loudness = -17; Build = {
        param($b)
        $D::Mallet($b, 0.000, (N 81), 0.55, 0.28, 0.7)
        $D::Mallet($b, 0.075, (N 86), 0.60, 0.38, 0.7)
        $D::Glass($b, 0.075, (N 98), 0.08, 0.30)
        $D::Reverb($b, 0.12, 0.40, 0.5, 0.6, 0.008)
    } },
    @{ Name = 'ui_cancel.mp3'; Length = 0.50; LowPass = 3500; Loudness = -18; Build = {
        param($b)
        $D::Mallet($b, 0.000, (N 74), 0.55, 0.24, 0.45)
        $D::Mallet($b, 0.070, (N 69), 0.50, 0.28, 0.35)
        $D::Reverb($b, 0.10, 0.35, 0.6, 0.6, 0.008)
    } },
    @{ Name = 'ui_toast.mp3'; Length = 0.70; Loudness = -19; Build = {
        param($b)
        $D::Glass($b, 0.000, (N 88), 0.60, 0.50)
        $D::Glass($b, 0.045, (N 95), 0.22, 0.36)
        $D::Reverb($b, 0.18, 0.45, 0.5, 0.7, 0.010)
    } },
    @{ Name = 'ui_pause.mp3'; Length = 0.60; Loudness = -18; Build = {
        param($b)
        $D::Pad($b, 0.000, 0.38, (N 62), 0.32, 0.015, 0.25, 2600, 300, 0.8)
        $D::Pad($b, 0.000, 0.38, (N 69), 0.25, 0.015, 0.25, 2600, 300, 0.8)
        $D::Noise($b, 0.000, 0.030, 0.22, 0.12, 1, 2500, 500, 0.9, 0.3)
        $D::Mallet($b, 0.000, (N 81), 0.22, 0.18, 0.3)
        $D::Mallet($b, 0.070, (N 74), 0.22, 0.25, 0.2)
        $D::Reverb($b, 0.14, 0.40, 0.6, 0.7, 0.010)
    } },
    @{ Name = 'ui_resume.mp3'; Length = 0.65; Loudness = -18; Build = {
        param($b)
        $D::Noise($b, 0.000, 0.120, 0.08, 0.10, 1, 500, 2800, 0.9, 0.3)
        $D::Pad($b, 0.000, 0.26, (N 62), 0.25, 0.12, 0.10, 300, 2600, 0.8)
        $D::Pad($b, 0.000, 0.26, (N 69), 0.20, 0.12, 0.10, 300, 2600, 0.8)
        $D::Glass($b, 0.130, (N 86), 0.45, 0.50)
        $D::Glass($b, 0.130, (N 93), 0.12, 0.36)
        $D::Reverb($b, 0.16, 0.40, 0.5, 0.7, 0.010)
    } },

    # ---- Cards: paper transients, felt table body, a hint of room. ----
    @{ Name = 'card_select.mp3'; Length = 0.18; Loudness = -17; Drive = 1.8; Build = {
        param($b)
        $D::Noise($b, 0.000, 0.0003, 0.010, 0.60, 1, 3800, 3000, 1.6, 0)
        $D::Noise($b, 0.000, 0.004, 0.030, 0.12, 2, 2500, 5000, 0.7, 0)
        $D::Mallet($b, 0.000, (N 90), 0.14, 0.050, 0.3)
        $D::Reverb($b, 0.05, 0.30, 0.6, 0.5, 0.004)
    } },
    @{ Name = 'card_deselect.mp3'; Length = 0.18; Loudness = -18; Drive = 1.4; Build = {
        param($b)
        $D::Noise($b, 0.000, 0.0003, 0.010, 0.50, 1, 2600, 2000, 1.6, 0)
        $D::Noise($b, 0.000, 0.004, 0.030, 0.10, 2, 4500, 2200, 0.7, 0)
        $D::Mallet($b, 0.000, (N 83), 0.12, 0.050, 0.3)
        $D::Reverb($b, 0.05, 0.30, 0.6, 0.5, 0.004)
    } },
    @{ Name = 'card_deal.mp3'; Length = 0.20; Loudness = -22; Build = {
        param($b)
        $D::Noise($b, 0.000, 0.018, 0.045, 0.35, 1, 5200, 1900, 1.1, 0.2)
        $D::Noise($b, 0.050, 0.0003, 0.010, 0.35, 0, 3000, 1500, 0.7, 0.2)
        $D::Thump($b, 0.050, 240, 150, 0.010, 0.18, 0.0005, 0.030)
        $D::Reverb($b, 0.05, 0.30, 0.6, 0.5, 0.004)
    } },
    @{ Name = 'card_play.mp3'; Length = 0.40; Loudness = -14; Drive = 1.6; Build = {
        param($b)
        $D::Noise($b, 0.000, 0.0003, 0.006, 0.18, 1, 3500, 3500, 1.2, 0)
        $D::Noise($b, 0.009, 0.0003, 0.006, 0.22, 1, 3000, 3000, 1.2, 0)
        $D::Noise($b, 0.016, 0.0004, 0.028, 0.90, 1, 2400, 1400, 0.8, 0.15)
        $D::Noise($b, 0.016, 0.0002, 0.006, 0.35, 2, 5000, 5000, 0.7, 0)
        $D::Thump($b, 0.016, 190, 100, 0.018, 0.55, 0.0005, 0.080)
        $D::Modal($b, 0.016, 240, 0.22, 0.0005, 0.07, [double[]](1.0, 2.13, 3.4), [double[]](1.0, 0.5, 0.25), [double[]](1.0, 0.6, 0.4))
        $D::Noise($b, 0.020, 0.004, 0.100, 0.10, 0, 1200, 400, 0.7, 0.4)
        $D::Reverb($b, 0.12, 0.45, 0.6, 0.6, 0.006)
    } },
    # Two knuckle knocks on the table: the real-world gesture for "不要".
    @{ Name = 'card_pass.mp3'; Length = 0.45; Loudness = -16; Build = {
        param($b)
        $D::Knock($b, 0.000, 185, 0.70)
        $D::Knock($b, 0.120, 178, 0.55)
        $D::Reverb($b, 0.10, 0.45, 0.6, 0.6, 0.006)
    } },
    @{ Name = 'card_hint.mp3'; Length = 0.90; Loudness = -17; Build = {
        param($b)
        $D::Glass($b, 0.000, (N 81), 0.35, 0.40)
        $D::Glass($b, 0.055, (N 86), 0.40, 0.45)
        $D::Glass($b, 0.110, (N 93), 0.42, 0.60)
        $D::Sparkle($b, 0.100, 0.30, 6, [double[]](98, 100, 102, 105), 0.07, 0.30)
        $D::Reverb($b, 0.22, 0.55, 0.5, 0.8, 0.012)
    } },

    # ---- Game flow ----
    @{ Name = 'game_invalid_move.mp3'; Length = 0.45; LowPass = 2200; Loudness = -15; Build = {
        param($b)
        $D::Mallet($b, 0.000, (N 58), 0.60, 0.12, 0.35)
        $D::Thump($b, 0.000, 160, 120, 0.010, 0.30, 0.0005, 0.050)
        $D::Mallet($b, 0.100, (N 55), 0.60, 0.16, 0.30)
        $D::Thump($b, 0.100, 140, 100, 0.010, 0.30, 0.0005, 0.060)
        $D::Reverb($b, 0.06, 0.40, 0.6, 0.6, 0.006)
    } },
    @{ Name = 'game_turn_prompt.mp3'; Length = 1.00; Loudness = -17; Build = {
        param($b)
        $D::Glass($b, 0.000, (N 86), 0.45, 0.55)
        $D::Glass($b, 0.100, (N 93), 0.50, 0.75)
        $D::EPiano($b, 0.100, (N 81), 0.18, 0.60, 0.3)
        $D::Reverb($b, 0.20, 0.55, 0.5, 0.8, 0.012)
    } },
    # Riffle shuffle, cascade, squaring the deck on the table, then a soft chime.
    @{ Name = 'game_round_start.mp3'; Length = 1.35; Loudness = -17; Build = {
        param($b)
        $D::Scatter($b, 0.000, 0.40, 30, 0.40, 2600, 4600, 0.003, 0.006, 0)
        $D::Noise($b, 0.000, 0.120, 0.25, 0.05, 1, 3000, 2500, 0.8, 0)
        $D::Noise($b, 0.400, 0.030, 0.08, 0.20, 1, 4200, 1500, 0.9, 0.2)
        $D::Thump($b, 0.500, 200, 120, 0.012, 0.40, 0.0005, 0.060)
        $D::Noise($b, 0.500, 0.0003, 0.012, 0.35, 0, 2500, 1200, 0.7, 0.3)
        $D::Thump($b, 0.580, 200, 120, 0.012, 0.25, 0.0005, 0.050)
        $D::Noise($b, 0.580, 0.0003, 0.010, 0.22, 0, 2500, 1200, 0.7, 0.3)
        $D::Glass($b, 0.660, (N 86), 0.30, 0.65)
        $D::Glass($b, 0.660, (N 93), 0.22, 0.70)
        $D::EPiano($b, 0.660, (N 74), 0.20, 0.70, 0.3)
        $D::Reverb($b, 0.14, 0.50, 0.5, 0.8, 0.010)
    } },
    # Plays together with win/lose, so it stays on the shared root D.
    @{ Name = 'game_round_end.mp3'; Length = 1.20; LowPass = 5000; Loudness = -19; Build = {
        param($b)
        $D::Thump($b, 0.000, 150, 85, 0.020, 0.55, 0.0005, 0.14)
        $D::Noise($b, 0.000, 0.0005, 0.050, 0.35, 0, 1800, 600, 0.7, 0.4)
        $D::EPiano($b, 0.005, (N 50), 0.30, 0.95, 0.2)
        $D::EPiano($b, 0.005, (N 62), 0.15, 0.85, 0.2)
        $D::Reverb($b, 0.16, 0.55, 0.5, 0.9, 0.012)
    } },

    # ---- Big moments ----
    @{ Name = 'event_bomb.mp3'; Length = 1.90; HighPass = 28; Drive = 1.8; Loudness = -8; Ceiling = 0.92; Build = {
        param($b)
        $D::Noise($b, 0.000, 0.0002, 0.015, 0.80, 2, 1500, 1500, 0.7, 0)
        $D::Thump($b, 0.000, 95, 34, 0.090, 1.00, 0.0008, 0.55)
        $D::Thump($b, 0.000, 240, 90, 0.015, 0.50, 0.0005, 0.08)
        $D::Noise($b, 0.002, 0.003, 0.55, 0.75, 0, 5000, 150, 0.8, 0.6)
        $D::Noise($b, 0.002, 0.002, 0.12, 0.35, 1, 1800, 600, 1.0, 0.2)
        $D::Scatter($b, 0.080, 0.45, 14, 0.12, 1500, 3500, 0.006, 0.012, 1)
        $D::Gong($b, 0.010, (N 45), 0.30, 1.40)
        $D::Reverb($b, 0.16, 0.70, 0.5, 1.0, 0.015)
    } },
    # Guzheng-style pentatonic glissando up two octaves into a bright bell chord.
    @{ Name = 'event_spring.mp3'; Length = 2.10; Loudness = -13; Build = {
        param($b)
        $notes = 74, 76, 78, 81, 83, 86, 88, 90, 93, 95, 98
        for ($i = 0; $i -lt $notes.Count; $i++) {
            $t = $i * 0.030
            $D::Pluck($b, $t, (N $notes[$i]), 0.32 + ($i * 0.015), 0.90, 0.55)
            $D::Glass($b, $t, (N $notes[$i]), 0.06, 0.40)
        }
        $D::Glass($b, 0.360, (N 86), 0.35, 1.20)
        $D::Glass($b, 0.360, (N 90), 0.28, 1.10)
        $D::Glass($b, 0.360, (N 93), 0.28, 1.10)
        $D::EPiano($b, 0.360, (N 74), 0.25, 1.30, 0.4)
        $D::EPiano($b, 0.360, (N 69), 0.20, 1.30, 0.3)
        $D::EPiano($b, 0.360, (N 50), 0.22, 1.40, 0.2)
        $D::Sparkle($b, 0.360, 0.70, 12, [double[]](98, 100, 102, 105, 107, 110), 0.06, 0.50)
        $D::Reverb($b, 0.26, 0.70, 0.45, 1.0, 0.015)
    } },
    @{ Name = 'event_win.mp3'; Length = 2.00; Loudness = -13; Build = {
        param($b)
        $arp = @(@(0.020, 74, 0.38), @(0.110, 78, 0.38), @(0.200, 81, 0.40))
        foreach ($n in $arp) {
            $D::EPiano($b, $n[0], (N $n[1]), $n[2], 0.90, 0.6)
            $D::Glass($b, $n[0], (N ($n[1] + 12)), 0.12, 0.60)
        }
        $D::EPiano($b, 0.310, (N 86), 0.45, 1.30, 0.6)
        $D::Glass($b, 0.310, (N 98), 0.18, 1.00)
        $D::EPiano($b, 0.310, (N 62), 0.25, 1.40, 0.3)
        $D::EPiano($b, 0.310, (N 69), 0.20, 1.30, 0.3)
        $D::Mallet($b, 0.310, (N 50), 0.25, 0.90, 0.2)
        $D::Sparkle($b, 0.330, 0.70, 10, [double[]](98, 102, 105, 110), 0.05, 0.55)
        $D::Reverb($b, 0.24, 0.70, 0.5, 0.95, 0.015)
    } },
    # Gentle fall into B minor (relative minor), shares the D root with round_end.
    @{ Name = 'event_lose.mp3'; Length = 1.80; LowPass = 2600; Loudness = -15; Build = {
        param($b)
        $D::EPiano($b, 0.020, (N 78), 0.35, 0.70, 0.35)
        $D::EPiano($b, 0.200, (N 74), 0.33, 0.80, 0.30)
        $D::EPiano($b, 0.380, (N 71), 0.33, 1.20, 0.30)
        $D::EPiano($b, 0.380, (N 66), 0.20, 1.20, 0.20)
        $D::EPiano($b, 0.380, (N 62), 0.15, 1.20, 0.20)
        $D::EPiano($b, 0.380, (N 59), 0.22, 1.40, 0.20)
        $D::Reverb($b, 0.22, 0.65, 0.5, 0.9, 0.015)
    } },
    # Two soft bubble pops, like a chat message arriving.
    @{ Name = 'event_ai_talk.mp3'; Length = 0.30; LowPass = 6000; Loudness = -19; Build = {
        param($b)
        $D::Thump($b, 0.000, 480, 1000, 0.020, 0.50, 0.002, 0.060)
        $D::Thump($b, 0.060, 700, 1300, 0.018, 0.35, 0.002, 0.050)
        $D::Glass($b, 0.070, (N 95), 0.10, 0.20)
        $D::Reverb($b, 0.10, 0.40, 0.5, 0.6, 0.006)
    } }
)

$seed = 20260930
foreach ($def in $defs) {
    $seed++
    $options = @{ Seed = $seed }
    foreach ($key in 'HighPass', 'LowPass', 'Drive', 'Loudness', 'Ceiling') {
        if ($def.ContainsKey($key)) { $options[$key] = $def[$key] }
    }
    New-Sfx -Name $def.Name -Length $def.Length -Build $def.Build @options
}

Get-ChildItem -LiteralPath $OutputDir -Filter *.wav | Remove-Item -Force

Write-Host "Done. Output: $OutputDir"

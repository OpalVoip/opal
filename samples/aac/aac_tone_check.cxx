/*
 * Encode a 440 Hz tone to AAC-48S and decode it.
 * The tone has to be present in the decoded PCM. No socket is opened.
 */

#include <ptlib.h>
#include <ptlib/pprocess.h>

#include <codec/opalplugin.h>
#include <codec/known.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "../../src/codec/aacmf_inc.cxx"

extern "C" struct PluginCodec_Definition * OpalCodecPlugin_GetCodecs(unsigned * count, unsigned version);
extern PluginCodec_LogFunction PluginCodec_LogFunctionInstance;


static int LogToStderr(unsigned level,
                       const char * /*file*/,
                       unsigned /*line*/,
                       const char * section,
                       const char * log)
{
  if (log != NULL && level <= 2)
    fprintf(stderr, "%s: %s\n", section != NULL ? section : "AAC", log);
  return 1;
}


static double Goertzel(const int16_t * samples, unsigned count, unsigned stride, double frequency)
{
  double omega = 2.0 * 3.14159265358979323846 * frequency / AAC_SAMPLE_RATE;
  double coeff = 2.0 * cos(omega);
  double s0 = 0;
  double s1 = 0;
  double s2 = 0;
  for (unsigned i = 0; i < count; ++i) {
    s0 = samples[i * stride] + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}


static bool IsAdts(const uint8_t * data, unsigned size)
{
  return size >= 2 && data[0] == 0xff && (data[1] & 0xf0) == 0xf0;
}


class AACToneCheck : public PProcess
{
  PCLASSINFO(AACToneCheck, PProcess);

  public:
    AACToneCheck()
      : PProcess("OPAL", "AAC tone check", 1, 0, ReleaseCode, 1)
    {
    }

    virtual void Main() override;
};


PCREATE_PROCESS(AACToneCheck);


void AACToneCheck::Main()
{
  PluginCodec_LogFunctionInstance = LogToStderr;

  unsigned count = 0;
  const PluginCodec_Definition * codecs = OpalCodecPlugin_GetCodecs(&count, PLUGIN_CODEC_VERSION);
  const PluginCodec_Definition * encoderDefn = NULL;
  const PluginCodec_Definition * decoderDefn = NULL;
  for (unsigned i = 0; i < count; ++i) {
    if (strcmp(codecs[i].sourceFormat, OPAL_PCM16S_48KHZ) == 0 &&
        strcmp(codecs[i].destFormat, OPAL_AAC48S) == 0)
      encoderDefn = &codecs[i];
    if (strcmp(codecs[i].sourceFormat, OPAL_AAC48S) == 0 &&
        strcmp(codecs[i].destFormat, OPAL_PCM16S_48KHZ) == 0)
      decoderDefn = &codecs[i];
  }

  if (encoderDefn == NULL || decoderDefn == NULL) {
    cerr << "AAC-48S encoder or decoder was not registered\n";
    SetTerminationValue(1);
    return;
  }

  void * encoder = encoderDefn->createCodec(encoderDefn);
  void * decoder = decoderDefn->createCodec(decoderDefn);
  if (encoder == NULL || decoder == NULL) {
    cerr << "Failed to create the AAC codec\n";
    if (encoder != NULL)
      encoderDefn->destroyCodec(encoderDefn, encoder);
    if (decoder != NULL)
      decoderDefn->destroyCodec(decoderDefn, decoder);
    SetTerminationValue(1);
    return;
  }

  const unsigned frameSamples = AAC_FRAME_SAMPLES * AAC_CHANNELS;
  const unsigned frames = 8; // about 170 ms
  std::vector<int16_t> pcm(frameSamples);
  std::vector<uint8_t> encoded(AAC_MAX_FRAME_BYTES);
  std::vector<int16_t> decoded;
  unsigned packets = 0;
  bool adts = false;
  bool failed = false;

  for (unsigned frame = 0; frame < frames && !failed; ++frame) {
    for (unsigned n = 0; n < AAC_FRAME_SAMPLES; ++n) {
      double t = (frame * AAC_FRAME_SAMPLES + n) / (double)AAC_SAMPLE_RATE;
      int16_t sample = (int16_t)(12000.0 * sin(2.0 * 3.14159265358979323846 * 440.0 * t));
      pcm[n * 2] = sample;
      pcm[n * 2 + 1] = sample;
    }

    unsigned fromLen = (unsigned)(pcm.size() * sizeof(int16_t));
    unsigned toLen = (unsigned)encoded.size();
    unsigned flags = 0;
    if (encoderDefn->codecFunction(encoderDefn, encoder, pcm.data(), &fromLen, encoded.data(), &toLen, &flags) == 0 ||
        fromLen != pcm.size() * sizeof(int16_t)) {
      cerr << "Encoder rejected frame " << frame << "\n";
      failed = true;
      break;
    }
    if (toLen == 0)
      continue;

    if (IsAdts(encoded.data(), toLen)) {
      cerr << "Encoder emitted ADTS instead of a raw AAC frame\n";
      adts = true;
      failed = true;
      break;
    }

    ++packets;
    std::vector<int16_t> pcmOut(frameSamples * 2);
    unsigned decodedLen = (unsigned)(pcmOut.size() * sizeof(int16_t));
    unsigned packetLen = toLen;
    flags = 0;
    if (decoderDefn->codecFunction(decoderDefn, decoder, encoded.data(), &packetLen, pcmOut.data(), &decodedLen, &flags) == 0) {
      cerr << "Decoder rejected packet " << packets << "\n";
      failed = true;
      break;
    }
    if ((decodedLen % sizeof(int16_t)) != 0) {
      cerr << "Decoder returned a partial sample\n";
      failed = true;
      break;
    }
    decoded.insert(decoded.end(), pcmOut.begin(), pcmOut.begin() + decodedLen / sizeof(int16_t));
  }

  encoderDefn->destroyCodec(encoderDefn, encoder);
  decoderDefn->destroyCodec(decoderDefn, decoder);

  if (!failed && packets == 0) {
    cerr << "Encoder produced no AAC frames\n";
    failed = true;
  }

  const unsigned window = AAC_FRAME_SAMPLES * 2;
  double bestRatio = 0;
  double bestRms = 0;
  if (!failed) {
    if (decoded.size() < window * AAC_CHANNELS) {
      cerr << "Decoder produced " << decoded.size() / AAC_CHANNELS << " samples, need at least " << window << "\n";
      failed = true;
    }
    else {
      unsigned starts[] = { 0, AAC_FRAME_SAMPLES, AAC_FRAME_SAMPLES * 2, AAC_FRAME_SAMPLES * 3 };
      for (unsigned s = 0; s < sizeof(starts) / sizeof(starts[0]); ++s) {
        unsigned offset = starts[s] * AAC_CHANNELS;
        if (offset + window * AAC_CHANNELS > decoded.size())
          continue;
        const int16_t * samples = decoded.data() + offset;
        double tone = Goertzel(samples, window, AAC_CHANNELS, 440.0);
        double other = Goertzel(samples, window, AAC_CHANNELS, 1000.0);
        double energy = 0;
        for (unsigned i = 0; i < window; ++i) {
          double value = samples[i * AAC_CHANNELS];
          energy += value * value;
        }
        double rms = sqrt(energy / window);
        double ratio = tone / (other + 1.0);
        if (ratio > bestRatio) {
          bestRatio = ratio;
          bestRms = rms;
        }
      }

      // AAC priming shifts the tone. Any window that is clearly a 440 Hz
      // sine, and loud enough not to be the priming silence, is a pass.
      if (bestRatio < 20.0 || bestRms < 2000.0) {
        cerr << "Decoded audio is not the 440 Hz tone (ratio " << bestRatio << ", rms " << bestRms << ")\n";
        failed = true;
      }
    }
  }

  if (failed || adts) {
    SetTerminationValue(1);
    return;
  }

  cout << "AAC-48S tone check passed: " << packets << " raw frames, 440 Hz ratio " << bestRatio << "\n";
  SetTerminationValue(0);
}

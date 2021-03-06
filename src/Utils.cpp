
#include "Utils.hpp"
#include "Log.hpp"
#include <botan/pem.h>
#include <botan/base64.h>
#include <botan/auto_rng.h>
#include <cstdio>
#include <stdexcept>
#include <sstream>


bool Utils::parse(const poptContext& pc)
{  // http://privatemisc.blogspot.com/2012/12/popt-basic-example.html
  poptSetOtherOptionHelp(pc, "[ARG...]");

  // process options and handle each val returned
  int val;
  while ((val = poptGetNextOpt(pc)) >= 0)
  {
  }

  // poptGetNextOpt returns -1 when the final argument has been parsed
  if (val != -1)
  {
    poptPrintUsage(pc, stderr, 0);

    // handle error
    switch (val)
    {
      case POPT_ERROR_NOARG:
        printf("Argument expected but missing for an option.\n");
        return false;
      case POPT_ERROR_BADOPT:
        printf("Failed to parse argument.\n");
        return false;
      case POPT_ERROR_BADNUMBER:
      case POPT_ERROR_OVERFLOW:
        printf("Option could not be converted to number\n");
        return false;
      default:
        printf("Unknown error in option processing\n");
        return false;
    }
  }

  poptFreeContext(pc);
  return true;
}



// https://stackoverflow.com/questions/6855115/byte-array-to-int-c
uint32_t Utils::arrayToUInt32(const uint8_t* byteArray, int32_t offset)
{
  return *reinterpret_cast<const uint32_t*>(&byteArray[offset]);
}



char* Utils::getAsHex(const uint8_t* data, int len)
{
  char* hexStr = new char[len * 2 + 3];
  hexStr[0] = '0';
  hexStr[1] = 'x';
  for (int i = 0; i < len; i++)
    sprintf(&hexStr[i * 2 + 2], "%02x", data[i]);
  return hexStr;
}



bool Utils::isPowerOfTwo(std::size_t x)
{  // glibc method of checking
  return ((x != 0) && !(x & (x - 1)));
}



unsigned long Utils::decode64Estimation(unsigned long inSize)
{  // https://stackoverflow.com/questions/1533113/calculate-the-size-to-a-base-64-encoded-message
  return ((inSize * 4) / 3) + (inSize / 96) + 6;
}



// https://stackoverflow.com/questions/1494399/how-do-i-search-find-and-replace-in-a-standard-string
void Utils::stringReplace(std::string& str,
                          const std::string& find,
                          const std::string& replace)
{
  size_t pos = 0;
  while ((pos = str.find(find, pos)) != std::string::npos)
  {
    str.replace(pos, find.length(), replace);
    pos += replace.length();
  }
}



// https://stackoverflow.com/questions/874134/
bool Utils::strEndsWith(const std::string& str, const std::string& ending)
{
  if (str.length() >= ending.length())
    return (
        0 ==
        str.compare(str.length() - ending.length(), ending.length(), ending));
  else
    return false;
}



bool Utils::strBeginsWith(const std::string& str, const std::string& begin)
{  // https://stackoverflow.com/questions/931827/
  return str.compare(0, begin.length(), begin) == 0;
}



std::string Utils::trimString(const std::string& str)
{  // https://stackoverflow.com/questions/216823
  std::string s(str);
  s.erase(s.begin(),
          std::find_if(s.begin(), s.end(),
                       std::not1(std::ptr_fun<int, int>(std::isspace))));
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       std::not1(std::ptr_fun<int, int>(std::isspace))).base(),
          s.end());
  return s;
}



Botan::RSA_PublicKey* Utils::base64ToRSA(const std::string& base64)
{
  // decode public key
  unsigned long expectedSize = decode64Estimation(base64.length());
  uint8_t* keyBuffer = new uint8_t[expectedSize];
  size_t len = Botan::base64_decode(keyBuffer, base64, false);

  // interpret and parse into public RSA key
  std::istringstream iss(std::string(reinterpret_cast<char*>(keyBuffer), len));
  Botan::DataSource_Stream keyStream(iss);
  return dynamic_cast<Botan::RSA_PublicKey*>(Botan::X509::load_key(keyStream));
}



Botan::RSA_PrivateKey* Utils::loadKey(const std::string& filename)
{
  static Botan::AutoSeeded_RNG rng;

  try
  {
    // attempt reading key as standardized PKCS8 format
    Log::get().notice("Opening HS key... ");

    auto pvtKey = Botan::PKCS8::load_key(filename, rng);
    auto rsaKey = dynamic_cast<Botan::RSA_PrivateKey*>(pvtKey);
    if (!rsaKey)
      Log::get().error("The loaded key is not a RSA key!");

    Log::get().notice("Read PKCS8-formatted RSA key.");
    return rsaKey;
  }
  catch (const Botan::Decoding_Error&)
  {
    Log::get().notice("Read OpenSSL-formatted RSA key.");
    return Utils::loadOpenSSLRSA(filename, rng);
  }
  catch (const Botan::Stream_IO_Error& err)
  {
    Log::get().error(err.what());
  }

  return NULL;
}



// http://botan.randombit.net/faq.html#how-do-i-load-this-key-generated-by-openssl-into-botan
// http://lists.randombit.net/pipermail/botan-devel/2010-June/001157.html
// http://lists.randombit.net/pipermail/botan-devel/attachments/20100611/1d8d870a/attachment.cpp
Botan::RSA_PrivateKey* Utils::loadOpenSSLRSA(const std::string& filename,
                                             Botan::RandomNumberGenerator& rng)
{
  Botan::DataSource_Stream in(filename);

  Botan::DataSource_Memory key_bits(
      Botan::PEM_Code::decode_check_label(in, "RSA PRIVATE KEY"));

  // Botan::u32bit version;
  size_t version;
  Botan::BigInt n, e, d, p, q;

  Botan::BER_Decoder(key_bits)
      .start_cons(Botan::SEQUENCE)
      .decode(version)
      .decode(n)
      .decode(e)
      .decode(d)
      .decode(p)
      .decode(q);

  if (version != 0)
    return NULL;

  return new Botan::RSA_PrivateKey(rng, p, q, e, d, n);
}



// This function assumes src to be a zero terminated sanitized string with
// an even number of [0-9a-f] characters, and target to be sufficiently large
void Utils::hex2bin(const uint8_t* src, uint8_t* target)
{  // https://stackoverflow.com/questions/17261798/
  while (*src && src[1])
  {
    *(target++) = char2int(*src) * 16 + char2int(src[1]);
    src += 2;
  }
}



uint8_t Utils::char2int(const uint8_t c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;

  Log::get().error("Invalid character");
  return 0;
}

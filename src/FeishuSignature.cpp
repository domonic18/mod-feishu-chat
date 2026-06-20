#include "FeishuSignature.h"

#include <ctime>
#include <vector>
#include <sstream>

#include "HMAC.h"
#include "Base64.h"

namespace ModFeishuChat
{
    void FeishuSignature::Compute(std::string const& secret,
                                  std::string& outTimestamp,
                                  std::string& outSign)
    {
        std::time_t now = std::time(nullptr);
        std::ostringstream oss;
        oss << now;
        outTimestamp = oss.str();

        std::string const stringToSign = outTimestamp + "\n" + secret;

        Acore::Crypto::HMAC_SHA256 hash(reinterpret_cast<uint8 const*>(secret.data()), secret.size());
        hash.UpdateData(reinterpret_cast<uint8 const*>(stringToSign.data()), stringToSign.size());
        hash.Finalize();

        auto digest = hash.GetDigest();
        std::vector<uint8> digestVec(digest.begin(), digest.end());
        outSign = Acore::Encoding::Base64::Encode(digestVec);
    }
}

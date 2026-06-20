#include "FeishuSignature.h"

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
        auto digest = Acore::Crypto::HMAC_SHA256::GetDigestOf(secret, stringToSign);

        std::vector<uint8> digestVec(digest.begin(), digest.end());
        outSign = Acore::Encoding::Base64::Encode(digestVec);
    }
}

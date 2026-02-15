#include "ctl/unicode.hpp"

//namespace ctl {
//
//    // ... autres méthodes ...
//
//    Ulen Rune::encode_utf8(Slice<Uint8> dest) const {
//        // 1. Validation de la plage Unicode
//        // On rejette tout ce qui dépasse le max officiel
//        if (v_ > 0x10FFFF) {
//            return 0; 
//        }
//        
//        // 2. Validation des Surrogates
//        // Ces valeurs sont réservées pour l'encodage UTF-16 et interdites en UTF-8
//        if (v_ >= 0xD800 && v_ <= 0xDFFF) {
//            return 0;
//        }
//
//        // Sécurité pointeur
//        if (dest.data() == nullptr) return 0;
//        auto out = dest.data();
//        
//        // --- Encodage ---
//
//        // 1 Octet (ASCII) : 0xxxxxxx
//        if (v_ < 0x80) {
//            if (dest.length() < 1) return 0;
//            out[0] = static_cast<Uint8>(v_);
//            return 1;
//        } 
//        
//        // 2 Octets : 110xxxxx 10xxxxxx
//        if (v_ < 0x800) {
//            if (dest.length() < 2) return 0;
//            // On masque ((v_ >> 6) & 0x1F) pour être sûr, même si v_ < 0x800 le garantit implicitement
//            out[0] = static_cast<Uint8>(0xC0 | ((v_ >> 6) & 0x1F)); 
//            out[1] = static_cast<Uint8>(0x80 | (v_ & 0x3F));
//            return 2;
//        } 
//        
//        // 3 Octets : 1110xxxx 10xxxxxx 10xxxxxx
//        if (v_ < 0x10000) {
//            if (dest.length() < 3) return 0;
//            out[0] = static_cast<Uint8>(0xE0 | ((v_ >> 12) & 0x0F));
//            out[1] = static_cast<Uint8>(0x80 | ((v_ >> 6) & 0x3F));
//            out[2] = static_cast<Uint8>(0x80 | (v_ & 0x3F));
//            return 3;
//        } 
//        
//        // 4 Octets : 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
//        // On n'a plus besoin du 'else' car on a validé v_ <= 0x10FFFF au début
//        if (dest.length() < 4) return 0;
//        out[0] = static_cast<Uint8>(0xF0 | ((v_ >> 18) & 0x07));
//        out[1] = static_cast<Uint8>(0x80 | ((v_ >> 12) & 0x3F));
//        out[2] = static_cast<Uint8>(0x80 | ((v_ >> 6) & 0x3F));
//        out[3] = static_cast<Uint8>(0x80 | (v_ & 0x3F));
//        return 4;
//    }
//
//}

namespace ctl {

    Bool Rune::is_char() const {
	if (v_ < 0x80) {
            if (v_ == '_') {
                return true;
            }
            return ((v_ | 0x20) - 0x61) < 26;
	}
	// TODO: LU, LL, LT, LM, LO => true
	return false;
    }

    Bool Rune::is_digit() const {
	if (v_ < 0x80) {
            return (v_ - '0') < 10;
	}
	// TODO: ND => true
	return false;
    }

    Bool Rune::is_digit(Uint32 base) const {
	if (v_ < 0x80) {
            switch (v_) {
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
                return v_ - '0' < base;
            case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
                return v_ - 'a' + 10 < base;
            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
                return v_ - 'A' + 10 < base;
            }
	}
	// TODO: ND => true
	return false;
    }

    Bool Rune::is_alpha() const {
	return is_char() || is_digit();
    }

    Bool Rune::is_white() const {
	return v_ == ' ' || v_ == '\t' || v_ == '\n' || v_ == '\r';
    }

    Ulen Rune::encode_utf8(Slice<Uint8> dest) const {
        // Validation unicode plage
        if (v_ > 0x10FFFF) {
            return 0;
        }

        // Surrogates validation
        if (v_ >= 0xD800 && v_ <= 0xDFFF) {
            return 0;
        }

        if (dest.data() == nullptr) return 0;
        auto out = dest.data();
        
        // 1 Byte (ASCII) : 0xxxxxxx
        if (v_ < 0x80) {
            if (dest.length() < 1) return 0;
            out[0] = static_cast<Uint8>(v_);
            return 1;
        } 
        // 2 Bytes : 110xxxxx 10xxxxxx
        else if (v_ < 0x800) {
            if (dest.length() < 2) return 0;
            out[0] = static_cast<Uint8>(0xC0 | (v_ >> 6));
            out[1] = static_cast<Uint8>(0x80 | (v_ & 0x3F));
            return 2;
        } 
        // 3 Bytes : 1110xxxx 10xxxxxx 10xxxxxx
        else if (v_ < 0x10000) {
            if (dest.length() < 3) return 0;
            out[0] = static_cast<Uint8>(0xE0 | (v_ >> 12));
            out[1] = static_cast<Uint8>(0x80 | ((v_ >> 6) & 0x3F));
            out[2] = static_cast<Uint8>(0x80 | (v_ & 0x3F));
            return 3;
        } 
        // 4 Bytes : 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        else {
            if (dest.length() < 4) return 0;
            out[0] = static_cast<Uint8>(0xF0 | (v_ >> 18));
            out[1] = static_cast<Uint8>(0x80 | ((v_ >> 12) & 0x3F));
            out[2] = static_cast<Uint8>(0x80 | ((v_ >> 6) & 0x3F));
            out[3] = static_cast<Uint8>(0x80 | (v_ & 0x3F));
            return 4;
        }
    }
    
    Ulen Rune::decode_utf8(Slice<const Uint8> src, Rune& rune) {
        if (src.length() == 0) return 0;

        const Uint8* data = src.data();
        Uint8 b0 = data[0];

        // 1 Byte (ASCII) : 0xxxxxxx
        if (b0 < 0x80) {
            rune = Rune(static_cast<Uint32>(b0));
            return 1;
        }

        // 2 Bytes : 110xxxxx 10xxxxxx
        if ((b0 & 0xE0) == 0xC0) {
            if (src.length() < 2) return 0;
            if ((data[1] & 0xC0) != 0x80) return 0; // invalid byte continuation
            
            Uint32 res = ((b0 & 0x1F) << 6) | (data[1] & 0x3F);
            if (res < 0x80) return 0; // encoding "overlong"
            
            rune = Rune(res);
            return 2;
        }

        // 3 Bytes : 1110xxxx 10xxxxxx 10xxxxxx
        if ((b0 & 0xF0) == 0xE0) {
            if (src.length() < 3) return 0;
            if ((data[1] & 0xC0) != 0x80 || (data[2] & 0xC0) != 0x80) return 0;

            Uint32 res = ((b0 & 0x0F) << 12) | ((data[1] & 0x3F) << 6) | (data[2] & 0x3F);
            if (res < 0x800) return 0; // Overlong
            if (res >= 0xD800 && res <= 0xDFFF) return 0; // Surrogates forbidden in UTF-8

            rune = Rune(res);
            return 3;
        }

        // 4 Bytes : 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        if ((b0 & 0xF8) == 0xF0) {
            if (src.length() < 4) return 0;
            if ((data[1] & 0xC0) != 0x80 || (data[2] & 0xC0) != 0x80 || (data[3] & 0xC0) != 0x80) return 0;

            Uint32 res = ((b0 & 0x07) << 18) | ((data[1] & 0x3F) << 12) | 
                ((data[2] & 0x3F) << 6) | (data[3] & 0x3F);
            if (res < 0x10000 || res > 0x10FFFF) return 0; // Overlong ou out of page

            rune = Rune(res);
            return 4;
        }

        return 0;
    }

    
} // namespace ctl

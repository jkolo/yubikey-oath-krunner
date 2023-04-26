using System.Security.Cryptography;
using Org.BouncyCastle.Crypto;
using Org.BouncyCastle.Crypto.Agreement;
using Org.BouncyCastle.Crypto.Engines;
using Org.BouncyCastle.Crypto.Modes;
using Org.BouncyCastle.Crypto.Paddings;
using Org.BouncyCastle.Crypto.Parameters;
using Org.BouncyCastle.Math;
using Org.BouncyCastle.Security;

namespace KRunner.YOath;

public class DHAesCbcPkcs7
{
    public class Encryptor
    {
        private readonly byte[] _aesKey;

        public Encryptor(byte[] aesKey)
        {
            _aesKey = aesKey;
        }

        public byte[] Encrypt(byte[] data, out byte[] iv)
        {
            iv = new byte[16];
            new SecureRandom().NextBytes(iv);
            
            var cipher = new PaddedBufferedBlockCipher(new CbcBlockCipher(new AesLightEngine()), new Pkcs7Padding());
            cipher.Init(true, new ParametersWithIV(new KeyParameter(_aesKey), iv));

            return cipher.DoFinal(data);
        }

        public byte[] Decrypt(byte[] data, byte[] iv)
        {
            var cipher = new PaddedBufferedBlockCipher(new CbcBlockCipher(new AesLightEngine()), new Pkcs7Padding());
            cipher.Init(false, new ParametersWithIV(new KeyParameter(_aesKey), iv));

            return cipher.DoFinal(data);
        }
    }
    
    private static readonly DHParameters SecondOakleyParameters = new(
        new("""
            FFFFFFFFFFFFFFFFC90FDAA22168C234
            C4C6628B80DC1CD129024E088A67CC74
            020BBEA63B139B22514A08798E3404DD
            EF9519B3CD3A431B302B0A6DF25F1437
            4FE1356D6D51C245E485B576625E7EC6
            F44C42E9A637ED6B0BFF5CB6F406B7ED
            EE386BFB5A899FA5AE9F24117C4B1FE6
            49286651ECE65381FFFFFFFFFFFFFFFF
            """.ReplaceLineEndings(string.Empty),
            16),
        BigInteger.Two);

    private readonly AsymmetricCipherKeyPair _dhKey = GenerateKeyPair();

    private static AsymmetricCipherKeyPair GenerateKeyPair()
    {
        var keyGen = GeneratorUtilities.GetKeyPairGenerator("DH");
        var kgp = new DHKeyGenerationParameters(new SecureRandom(), SecondOakleyParameters);
        keyGen.Init(kgp);
        return keyGen.GenerateKeyPair();
    }

    public byte[] PublicKey => ((DHPublicKeyParameters)_dhKey.Public).Y.ToByteArrayUnsigned();

    public Encryptor CreateEncryptor(byte[] publicKey)
    {
        var importedKey = new DHPublicKeyParameters(new BigInteger(1, publicKey), SecondOakleyParameters);
        var internalKeyAgree = new DHBasicAgreement();
        internalKeyAgree.Init(_dhKey.Private);
        var ret = internalKeyAgree.CalculateAgreement(importedKey).ToByteArrayUnsigned();
        var aesKey = new byte[16];
        HKDF.DeriveKey(HashAlgorithmName.SHA256, ret, aesKey, Array.Empty<byte>(), Array.Empty<byte>());
        return new Encryptor(aesKey);
    }
}

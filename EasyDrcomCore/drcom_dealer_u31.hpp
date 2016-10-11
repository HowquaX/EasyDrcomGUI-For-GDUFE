/**
 * Copyright (C) 2014, 2015 Shindo
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifndef __INCLUDE_DRCOM_DEALER_BASE_U31__
#define __INCLUDE_DRCOM_DEALER_BASE_U31__

class drcom_dealer_u31 : public drcom_dealer_base {
public:
    drcom_dealer_u31(std::vector<uint8_t> local_mac, std::string local_ip, std::string username, std::string password,
                     std::string gateway_ip, uint32_t gateway_port, std::string hostname, std::string kernel_version
                     ) : local_mac(local_mac), local_ip(str_ip_to_vec(local_ip)),
    hostname(hostname), kernel_version(kernel_version),
    username(username), password(password),
    total_time(0), total_flux(0), balance(0), online_time(0), pkt_id(0), misc1_flux(0), misc3_flux(0),
    udp(gateway_ip, gateway_port, local_ip)
    {}
    
    int start_request()
    {
        U31_LOG_INFO("Start Request." << std::endl);
        
        std::vector<uint8_t> pkt_data;
        
        // fixed length = 20
        //                                Code, Retry, N/A,  N/A, Version
        pkt_data.insert(pkt_data.end(), { 0x01, 0x00, 0x00, 0x00, 0x22 }); //GDUFE
        pkt_data.insert(pkt_data.end(), 15, 0x00);
        
        challenge.clear();
        
        auto handler_success = [&](std::vector<uint8_t> recv) -> int {
            U31_LOG_RECV_DUMP("Start Request");

			if (recv[0] == 0x4d) // Notification
			{
				U31_LOG_INFO("Received 'Notification', Send Start Request again." << std::endl);
				return start_request();
			}
            
            if (recv[0] != 0x02) // Start Response
                return -1;
            
            U31_LOG_INFO("Gateway return: Start Response." << std::endl);
            challenge.resize(4);
            memcpy(&challenge[0], &recv[4], 4); // Challenge
            return 0;
        };
        
        U31_HANDLE_ERROR("Start Request");
        U31_PERFORM("Start Request");
    }
    
    int send_login_auth()
    {
        if (challenge.empty()) return -1;
        
        U31_LOG_INFO("Send Login Auth." << std::endl);
        U31_LOG_DBG("username = " << username << ", password = " << password << std::endl);
        
        auth_info.clear();
        
        std::vector<uint8_t> pkt_data;
        
        /********************** Header *************************/
        //                                  Code, Type, EOF,  UserName Length + 20
        auto length = ((username.length() <= 36) ? username.length() : 36) + 20;
        pkt_data.insert(pkt_data.end(), { 0x03, 0x01, 0x00, (uint8_t) length });
        
        /********************** MD5A **************************/
        // Function_MD5A = MD5(code + type + Challenge + Password)
        std::vector<uint8_t> md5a_content = { 0x03, 0x01 /* Code, Type */ };
        md5a_content.insert(md5a_content.end(), challenge.begin(), challenge.end());
        md5a_content.insert(md5a_content.end(), password.begin(), password.end());
        
        login_md5_a = get_md5_digest(md5a_content);
        pkt_data.insert(pkt_data.end(), login_md5_a.begin(), login_md5_a.end());
        
        /********************** UserName *********************/
        std::vector<uint8_t> username_block(36, 0); // fixed length = 36
        memcpy(&username_block[0], &username[0], username.length() <= 36 ? username.length() : 36);
        pkt_data.insert(pkt_data.end(), username_block.begin(), username_block.end());
        
        /********************** Conf *************************/
        //                                0x20, 0x05        On Windows
        //pkt_data.insert(pkt_data.end(), { 0x00, 0x00 });//  On OSX
		pkt_data.insert(pkt_data.end(), { 0x20, 0x04 });//  GDUFE

        /********************** MAC xor MD5A *****************/
        for (int i = 0; i < local_mac.size(); i++)
            pkt_data.push_back(local_mac[i] ^ login_md5_a[i]);
        
        /********************** MD5B *************************/
        // Function MD5B = MD5(0x01 + password + challenge + 0x00 *4)
        std::vector<uint8_t> md5b_content = { 0x01 };
        md5b_content.insert(md5b_content.end(), password.begin(), password.end());
        md5b_content.insert(md5b_content.end(), challenge.begin(), challenge.end());
        md5b_content.insert(md5b_content.end(), 4, 0x00);
        
        auto login_md5_b = get_md5_digest(md5b_content);
        pkt_data.insert(pkt_data.end(), login_md5_b.begin(), login_md5_b.end());
        
        /********************** NIC **************************/
        pkt_data.push_back(1); // NIC Count
        // 4 NIC's IPs in total
        pkt_data.insert(pkt_data.end(), local_ip.begin(), local_ip.end());
        pkt_data.insert(pkt_data.end(), 12, 0x00); // Fill remaining 3 NIC's IP with zero.
        
        /********************** Checksum 1 *******************/
        std::vector<uint8_t> checksum_content(pkt_data);
        checksum_content.insert(checksum_content.end(), { 0x14, 0x00, 0x07, 0x0b }); // MD5 Tail
        
        auto checksum_1 = get_md5_digest(checksum_content);
        pkt_data.insert(pkt_data.end(), checksum_1.begin(), checksum_1.begin() + 8); // Only need 8 bytes
        
        /********************** IP Dog & Fill ***************/
        pkt_data.push_back(0x01); // IP Dog
        pkt_data.insert(pkt_data.end(), 4, 0x00); // Fill 0x00 *4
        
        /********************** Host Name *******************/
        // fixed length = 32
        std::vector<uint8_t> hostname_block(32, 0);
        memcpy(&hostname_block[0], &hostname[0], hostname.length() <= 32 ? hostname.length() : 32);
        pkt_data.insert(pkt_data.end(), hostname_block.begin(), hostname_block.end());
        
        /********************** DNS & DHCP & Fill redited for GDUFE***********/
		pkt_data.insert(pkt_data.end(), { 0xca, 0x60, 0x80, 0xa6 }); // Primary DNS
		pkt_data.insert(pkt_data.end(), { 0xde, 0xca, 0xab, 0x21 }); // DHCP
		pkt_data.insert(pkt_data.end(), { 0xde, 0xca, 0xab, 0x1f }); // Secondary DNS
        pkt_data.insert(pkt_data.end(), 8, 0x00); // Fill 0x00 *8
        
        /********************** Host System Info & Fill *****/
		pkt_data.insert(pkt_data.end(), { 0x94, 0x00, 0x00, 0x00 }); // Unknown 1
		pkt_data.insert(pkt_data.end(), { 0x00, 0x00, 0x00, 0x00 }); // OS major
		pkt_data.insert(pkt_data.end(), { 0x00, 0x00, 0x00, 0x00 }); // OS minor
		pkt_data.insert(pkt_data.end(), { 0x00, 0x00, 0x00, 0x00 }); // OS build
		pkt_data.insert(pkt_data.end(), { 0x02, 0x00, 0x00, 0x00 }); // Unknown 2
        
		pkt_data.insert(pkt_data.end(), 64, 0x00); // Fill 0x00 *64

		pkt_data.insert(pkt_data.end(), { 0x37,0x34,0x30,0x30,0x64,0x31,0x35,0x32 });
		pkt_data.insert(pkt_data.end(), { 0x61,0x32,0x32,0x39,0x31,0x37,0x64,0x32 });
		pkt_data.insert(pkt_data.end(), { 0x30,0x62,0x64,0x33,0x39,0x63,0x39,0x34 });
		pkt_data.insert(pkt_data.end(), { 0x37,0x61,0x66,0x61,0x32,0x63,0x35,0x35 });
		pkt_data.insert(pkt_data.end(), { 0x63,0x63,0x33,0x39,0x32,0x38,0x66,0x30 });
        
        pkt_data.insert(pkt_data.end(), 24, 0x00); // Fill 0x00 *24
        
        /********************** Checksum 2 *****************/
        //                            Version redited for GDUFE
		// by setting the version info to 0xff for bypassing check 
		std::vector<uint8_t> checksum_2 = { 0xff, 0xff, 0xff, 0xff };
        checksum_2.insert(checksum_2.end(), checksum_1.begin() + 10, checksum_1.begin() + 10 + 4); // 4 bytes from Checksum1
        checksum_2.insert(checksum_2.end(), { 0x00, 0x00 } ); // Unkown: 2 bytes
        pkt_data.insert(pkt_data.end(), checksum_2.begin(), checksum_2.end());
        
        /********************** MAC ************************/
        pkt_data.insert(pkt_data.end(), local_mac.begin(), local_mac.end());
        
        /********************** Conf ***********************/
        pkt_data.insert(pkt_data.end(), { 0x00, 0x00 }); // Auto_Logout, Multicast_Mode
        pkt_data.insert(pkt_data.end(), { 0x00, 0x00 }); // Unkown: 2 bytes
        
        U31_LOG_SEND_DUMP
        
        auto handler_success = [&](std::vector<uint8_t> recv) -> int {
            U31_LOG_RECV_DUMP("Send Login Auth");
			
            if (recv[0] != 0x04 && recv[0] != 0x05) // Success/Failure
                return -1;
            
            if (recv[0] == 0x05) // Failure
            {
                switch (recv[4])
                {
                    case 0x01: // Already online
                        U31_LOG_ERR("This account has already been online at IP: " << (int)recv[5] << "." << (int)recv[6] << "." << (int)recv[7] << "." << (int)recv[8] << ", MAC: " << hex_to_str(&recv[9], 6, ':') << std::endl);
                        break;
                        
                    case 0x03: // Username or password wrong!
                        U31_LOG_ERR("Username or password wrong!" << std::endl);
                        break;
                        
                    case 0x05: // No money
                        U31_LOG_ERR("No money in your account!" << std::endl);
                        break;
                        
                    case 0x0b: // Wrong MAC
                        U31_LOG_ERR("Wrong MAC, should be " << hex_to_str(&recv[5], 6, ':') << std::endl);
                        break;
                        
                    default:
                        U31_LOG_ERR("Unkown failure: 0x" << std::hex << (unsigned int)recv[4] << std::endl);
                        break;
                }
                return (unsigned int)recv[4]; // Don't retry when failure
            }
            
            U31_LOG_INFO("Gateway return: Success." << std::endl);
            
            // Success
            auth_info.insert(auth_info.end(), recv.begin() + 23, recv.begin() + 23 + 16); // 16 bytes from Success
            
            // Captured
            memcpy(&total_time, &recv[5], 4);
            memcpy(&total_flux, &recv[9], 4);
            memcpy(&balance, &recv[13], 4);
            
#ifdef OPENWRT
            // network order on openwrt
            total_time = TO_LITTLE_ENDIAN(total_time);
            total_flux = TO_LITTLE_ENDIAN(total_flux);
            balance = TO_LITTLE_ENDIAN(balance);
#endif
            
            U31_LOG_INFO("Login auth succeeded! User info: " << std::endl);
            U31_LOG_INFO("Used Time: " << total_time << " Minutes, Used Flux: " << (total_flux & 0x0FFFFFFFF) / 1024.0 << " MB, Balance: " << (balance & 0x0FFFFFFFF) / 100.0 << " RMB" << std::endl);
            
            return 0;
        };
        
        U31_HANDLE_ERROR("Send Login Auth");
        U31_PERFORM("Send Login Auth");
    }
//add the support to U31�����Ӷ�U31Э���֧��
	int recv_message()
	{
		std::vector<uint8_t> recv = udp.recv();
		if (recv.empty()) return 0;
		else if (recv[0] == 0x4d)
		{
			U31_LOG_INFO("Received A Message Packet" << std::endl);
			U31_LOG_RECV_DUMP("Message Packet");
			return -1;
		}
		else
		{
			U31_LOG_INFO("Received Unknown Packet" << std::endl);
			U31_LOG_RECV_DUMP("Unknown Packet");
			return -1;
		}
	}
    
    int send_alive_pkt1()
    {
        U31_LOG_INFO("Send Alive Packet 1." << std::endl);
        
        std::vector<uint8_t> pkt_data;
        pkt_data.push_back(0x07); // Code
        pkt_data.push_back(pkt_id);
		pkt_data.insert(pkt_data.end(), { 0x28, 0x00 }); // Type
		pkt_data.insert(pkt_data.end(), { 0x0B, 0x01 }); // Step
		pkt_data.insert(pkt_data.end(), { 0xDC, 0x02 }); // Fixed Unknown for GDUFE
		pkt_data.insert(pkt_data.end(), { 0x00, 0x00 }); // Unkown
		pkt_data.insert(pkt_data.end(), { 0x00, 0x00, 0x00, 0x00 }); // some time
		pkt_data.insert(pkt_data.end(), { 0x00, 0x00 }); // Fixed Unknown
        
        // some flux
        pkt_data.insert(pkt_data.end(), 4, 0x00);
        memcpy(&pkt_data[16], &misc1_flux, 4);
        
        pkt_data.insert(pkt_data.end(), 8, 0x00); // Fixed Unknown, 0x00 *8
        pkt_data.insert(pkt_data.end(), { 0x00, 0x00, 0x00, 0x00 }); // Client IP (Fixed: 0.0.0.0)
        pkt_data.insert(pkt_data.end(), 8, 0x00); // Fixed Unknown, 0x00 *8
        
        U31_LOG_SEND_DUMP
        
        auto handler_success = [&](std::vector<uint8_t> recv) -> int {
            U31_LOG_RECV_DUMP("Alive Packet 1");
            
            if (recv[0] != 0x07) // Misc
                return -1;
            
            if (recv[5] == 0x06) // File
            {
                U31_LOG_INFO("Received 'Misc, File', Send Keep Alive Packet 1 again." << std::endl);
                return send_alive_pkt1();
            }
            else
            {
                U31_LOG_INFO("Gateway return: Response for Alive Packet 1." << std::endl);
                
                pkt_id++;
                U31_LOG_DBG("next packet id = " << (int) pkt_id << std::endl);
                
                memcpy(&misc3_flux, &recv[16], 4);
                return 0;
            }
        };
        
        U31_HANDLE_ERROR("Send Alive Packet 1");
        U31_PERFORM("Send Alive Packet 1");
    }
    
    int send_alive_pkt2()
    {
        U31_LOG_INFO("Send Alive Packet 2." << std::endl);
        
        std::vector<uint8_t> pkt_data;
        pkt_data.push_back(0x07); // Code
		pkt_data.push_back(pkt_id);
		pkt_data.insert(pkt_data.end(), { 0x28, 0x00 }); // Type
		pkt_data.insert(pkt_data.end(), { 0x0B, 0x03 }); // Step
		pkt_data.insert(pkt_data.end(), { 0xDC, 0x02 }); // Fixed Unknown for GDUFE
		pkt_data.insert(pkt_data.end(), { 0x00, 0x00 }); // Unkown
		pkt_data.insert(pkt_data.end(), { 0x00, 0x00, 0x00, 0x00 }); // some time
		pkt_data.insert(pkt_data.end(), { 0x00, 0x00 }); // Fixed Unknown
        
        // some flux
		pkt_data.insert(pkt_data.end(), 4, 0x00);
		memcpy(&pkt_data[16], &misc3_flux, 4);

		pkt_data.insert(pkt_data.end(), 4, 0x00); // Fixed Unknown, 0x00 *4
		pkt_data.insert(pkt_data.end(), { 0x7c, 0x24, 0x09, 0x02 });//Unknown for GDUFE
		pkt_data.insert(pkt_data.end(), local_ip.begin(), local_ip.end()); // Client IP
		pkt_data.insert(pkt_data.end(), 8, 0x00); // Fixed Unknown, 0x00 *8
        
        U31_LOG_SEND_DUMP
        
        auto handler_success = [&](std::vector<uint8_t> recv) -> int {
            U31_LOG_RECV_DUMP("Alive Packet 2");
            
            if (recv[0] != 0x07 && recv[5] != 0x04) // Misc 4
                return -1;
            
            U31_LOG_INFO("Gateway return: Response for Alive Packet 2." << std::endl);
            
            pkt_id++;
            U31_LOG_DBG("next packet id = " << (int) pkt_id << std::endl);
            
            memcpy(&misc1_flux, &recv[16], 4);
            return 0;
        };
        
        U31_HANDLE_ERROR("Send Alive Packet 2");
        U31_PERFORM("Send Alive Packet 2");
    }
    
    int send_alive_request()
    {
        if (login_md5_a.empty()) return -1;
        if (auth_info.empty()) return -1;
        
        U31_LOG_INFO("Send Alive Request." << std::endl);
        
        std::vector<uint8_t> pkt_data;
        pkt_data.push_back(0xFF); // Code
        pkt_data.insert(pkt_data.end(), login_md5_a.begin(), login_md5_a.end());
        pkt_data.insert(pkt_data.end(), 3, 0x00); // Fill 0x00 *3
        pkt_data.insert(pkt_data.end(), auth_info.begin(), auth_info.end());
        pkt_data.insert(pkt_data.end(), 2, 0x00); // Fill 0x00 *2 for timestamp
        
        uint16_t now_time = (uint16_t)(time(NULL) % 86400);
        
        U31_LOG_SEND_DUMP
        
        auto handler_success = [&](std::vector<uint8_t> recv) -> int {
            U31_LOG_RECV_DUMP("Send Alive Request");
            
			if (recv[0] == 0x4d) // Notification
			{
				U31_LOG_INFO("Received 'Notification', Send Keep Alive Request again." << std::endl);
				return send_alive_request();
			}

            if (recv[0] != 0x07 && recv[5] != 0x00) // Response for Alive
                return -1;
            
            U31_LOG_INFO("Gateway return: Response for alive." << std::endl);
            
            // Captured
            memcpy(&online_time, &recv[32], 4);
            memcpy(&total_time, &recv[44], 4);
            memcpy(&total_flux, &recv[48], 4);
            memcpy(&balance, &recv[52], 4);
            
#ifdef OPENWRT
            // network order on openwrt
            online_time = TO_LITTLE_ENDIAN(online_time);
            total_time = TO_LITTLE_ENDIAN(total_time);
            total_flux = TO_LITTLE_ENDIAN(total_flux);
            balance = TO_LITTLE_ENDIAN(balance);
#endif
            
            U31_LOG_INFO("Keep Alive succeeded! Timestamp = " << now_time << ", user info:" << std::endl);
            U31_LOG_INFO("Online Time: " << online_time << " Seconds, Used Time: " << total_time << " Minutes, Used Flux: " << (total_flux & 0x0FFFFFFFF) / 1024.0 << " MB, Balance: " << (balance & 0x0FFFFFFFF) / 10000.0 << " RMB" << std::endl);
            
            return 0;
        };
        
        U31_HANDLE_ERROR("Send Alive Request");
        U31_PERFORM("Send Alive Request");
    }
    
    int send_logout_auth()
    {
        if (challenge.empty()) return -1;
        if (auth_info.empty()) return -1;
        
        U31_LOG_INFO("Send Logout Auth." << std::endl);
        
        std::vector<uint8_t> pkt_data;
        
        /************************ Header *******************/
        //                                Code, Type, EOF,  UserName Length + 20
        auto length = ((username.length() <= 36) ? username.length() : 36) + 20;
        pkt_data.insert(pkt_data.end(), { 0x06, 0x01, 0x00, (uint8_t) length });
        
        /********************** MD5A **************************/
        // Function_MD5A = MD5(code + type + Challenge + Password)
        std::vector<uint8_t> md5a_content = { 0x06, 0x01 /* Code, Type */ };
        md5a_content.insert(md5a_content.end(), challenge.begin(), challenge.end());
        md5a_content.insert(md5a_content.end(), password.begin(), password.end());
        
        login_md5_a = get_md5_digest(md5a_content);
        pkt_data.insert(pkt_data.end(), login_md5_a.begin(), login_md5_a.end());
        
        /********************** UserName *********************/
        std::vector<uint8_t> username_block(36, 0); // fixed length = 36
        memcpy(&username_block[0], &username[0], username.length() <= 36 ? username.length() : 36);
        pkt_data.insert(pkt_data.end(), username_block.begin(), username_block.end());
        
        /********************** Conf *************************/
        //                                0x20, 0x05        On Windows
        pkt_data.insert(pkt_data.end(), { 0x00, 0x00 });//  On OSX
        
        /********************** MAC xor MD5A *****************/
        for (int i = 0; i < local_mac.size(); i++)
            pkt_data.push_back(local_mac[i] ^ login_md5_a[i]);
        
        /********************** Auth Info ********************/
        pkt_data.insert(pkt_data.end(), auth_info.begin(), auth_info.end());
        
        U31_LOG_SEND_DUMP
        
        auto handler_success = [&](std::vector<uint8_t> recv) -> int {
            U31_LOG_RECV_DUMP("Send Logout Auth");
            
            if (recv[0] != 0x04) // Success
                return -1;
            
            U31_LOG_INFO("Logged out." << std::endl);
            challenge.clear(); login_md5_a.clear(); auth_info.clear();
            total_time = total_flux = balance = online_time = pkt_id = misc3_flux = misc1_flux = 0;
            return 0;
        };
        
        U31_HANDLE_ERROR("Send Logout Auth");
        U31_PERFORM("Send Logout Auth");
    }
    
private:
    udp_dealer udp;
    
    // Const
    std::vector<uint8_t> local_mac, local_ip;
    std::string username, password, hostname, kernel_version;
    
    // Send Login Auth
    std::vector<uint8_t> challenge;
    
    // Used by Alive
    std::vector<uint8_t> login_md5_a;
    
    // Recv from Success
    std::vector<uint8_t> auth_info;
    
    // Update from Succes & Alive
    uint32_t total_time, total_flux, balance, online_time;
    
    // Send Misc1, 3
    uint8_t pkt_id;
    uint32_t misc1_flux, misc3_flux;
};

#endif

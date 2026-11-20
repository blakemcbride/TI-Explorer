/* Local Bus header

 This file is subject to the terms and conditions of the GNU General Public
 License.  See the file COPYING in the main directory of this archive for
 more details.

*/

/* local bus Interface */
extern int LCbus_error;
extern int LCbus_acknowledge;
extern unsigned int LCbus_Address;
extern unsigned int LCbus_Request;
extern unsigned int LCbus_Data;

extern uint32 nubus_io_request(int access, uint32 address, uint32 data, int owner);

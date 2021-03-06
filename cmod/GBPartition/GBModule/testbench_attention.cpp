/*
 * All rights reserved - Harvard University. 
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the "License"); 
 * you may not use this file except in compliance with the License.  
 * You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "GBModule.h"
#include <systemc.h>
#include <mc_scverify.h>
#include <testbench/nvhls_rand.h>
#include <nvhls_connections.h>
#include <map>
#include <vector>
#include <deque>
#include <utility>
#include <sstream>
#include <string>
#include <cstdlib>
#include <math.h> // testbench only
#include <queue>
#include <iomanip>
#include <nvhls_int.h>
#include <nvhls_types.h>
#include <nvhls_vector.h>

#include "SM6Spec.h"
#include "AxiSpec.h"
#include "AdpfloatSpec.h"
#include "AdpfloatUtils.h"

#include "helper.h"

#include "../../testbench/libnpy/npy.hpp"

#define NVHLS_VERIFY_BLOCKS (GBModule)
#include <nvhls_verify.h>


#ifdef COV_ENABLE
   #pragma CTC SKIP
#endif

SC_MODULE(Source) {
  sc_in<bool> clk;
  sc_in<bool> rst;  
  Connections::Out<spec::StreamType> data_in;  
  Connections::Out<bool> pe_done; 
  Connections::Out<spec::Axi::SlaveToRVA::Write> rva_in;

  SC_CTOR(Source) {
    SC_THREAD(attention_run);
    sensitive << clk.pos();
    async_reset_signal_is(rst, false);
  }

  void attention_run() {
    spec::Axi::SlaveToRVA::Write  rva_in_src;

    std::string filename;
    std::vector<std::vector<double>>  encoder_source, encoder_source_pad, decoder_2d;
    std::vector<double>  decoder;
    std::vector<unsigned long> npy_shape;
    std::vector<double>  npy_data;

    spec::VectorType act_vec;
    spec::VectorType decoder_vec;
    //spec::VectorType beta_vec;

    AdpfloatType<8, 3> adpfloat_tmp;
    spec::AdpfloatBiasType adpbias_enc = 2;
    spec::AdpfloatBiasType adpbias_dec = 4;
    //spec::AdpfloatBiasType adpbias_softmax_out = 2;
    //spec::AdpfloatBiasType adpbias_output = 0;
    wait();

    //Attention AXI GBControlConfig 
    rva_in_src.rw = 1;
    rva_in_src.data = set_bytes<16>("00_02_04_02_00_00_00_60_00_10_00_00_00_00_00_01"); //is_valid=1, mode=0, is_rnn=0, memory_index1=0, memory_index2=0, num_vector= 16, num_output_vector=0, num_timestep=96, num_timestep_padding=00, adpbias_1=2, adpbias_2=4, adpbias_3 = 2, adpbias_4=0
    rva_in_src.addr = set_bytes<3>("B0_00_10");  // last 4 bits never used 
    rva_in.Push(rva_in_src);
    wait(); 

    //GBCore LargeBuffer Config
    rva_in_src.rw = 1;
    rva_in_src.data = set_bytes<16>("00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_10"); //num_vector_large for kMaxNumManagers=0 is 16, base_large for kMaxNumManagers=0 is 0 
    rva_in_src.addr = set_bytes<3>("40_00_10");  // last 4 bits never used 
    rva_in.Push(rva_in_src);
    wait(); 

    //Storing Activations in LargeBuffer
    npy_shape.clear(); npy_data.clear();
    npy::LoadArrayFromNumpy("/group/ttambe/sm6/hls_tapeout/cmod/testbench/attention_enc256_dec256/h_s_forbin.npy", npy_shape, npy_data);
    cout << "Encoder source shape is: " << npy_shape[1] << " by " << npy_shape[2] << endl;
    encoder_source = to_2d(npy_shape[1], npy_shape[2], npy_data);
    encoder_source_pad = MatrixPadding(encoder_source, 16);
    cout << "vector 0 after padding" << endl;
    PrintVector(encoder_source_pad[0]);
    //cout << "vector 83 after padding" << endl;
    //PrintVector(encoder_source_pad[83]);

    for (unsigned int i=0; i < 6; i++) {  // i --> upper_timestepindex 
      for (int j=0; j < 16; j++) { //j --> vector_index
        for (int m=0; m < spec::kNumVectorLanes; m++) { //m --> bank_index

               for (int k=0; k < spec::kNumVectorLanes; k++) { //k --> scalar_index
                 adpfloat_tmp.Reset();
                 adpfloat_tmp.set_value(encoder_source_pad[16*i+m][16*j + k], adpbias_enc);
                 act_vec[k] =adpfloat_tmp.to_rawbits();
                 adpfloat_tmp.Reset();
               } // for k
           rva_in_src.rw = 1;
           rva_in_src.data = act_vec.to_rawbits();
           rva_in_src.addr = 0x500000 + (16*(16*i + j) + m)*16;
           //cout << "addresss_wii: " << 16*(16*i + j) + m << " with i " << i << " j: " << j << " m: " << m << endl;
           rva_in.Push(rva_in_src);
           if (rva_in_src.addr ==  0x500000 + (16*(16*5 + 15) + 15)*16) {
              cout << "Pushed last encoder_source activation vector: " << endl;
           }
           wait();
        } // for j
      } // for m
    } // for i 
    wait(); 

    //GBCore SmallBuffer Config
    rva_in_src.rw = 1;
    rva_in_src.data = set_bytes<16>("00_10_00_00_00_00_00_00_00_00_00_00_00_00_00_00"); //base_memory_index2 for decoder = 0, base_softmax_index at index7 = 16 
    rva_in_src.addr = set_bytes<3>("40_00_20");  // last 4 bits never used 
    rva_in.Push(rva_in_src);
    wait(); 

    //Storing gamma in SmallBuffer
    npy_shape.clear(); npy_data.clear();
    npy::LoadArrayFromNumpy("/group/ttambe/sm6/hls_tapeout/cmod/testbench/attention_enc256_dec256/h_t_forbin.npy", npy_shape, npy_data);
    decoder_2d =  to_2d(npy_shape[1], npy_shape[2], npy_data);
    cout << "decoder_2d shape is: " << npy_shape[1] << " by " << npy_shape[2] << endl;
    decoder = decoder_2d[0];
    cout << "size of decoder: " << decoder.size() << endl;
    cout << "printing decoder values from npy file: " << endl;
    PrintVector(decoder);

    for (unsigned int i=0; i < 16; i++) { 
        for (int j=0; j < spec::kNumVectorLanes; j++) {
           adpfloat_tmp.Reset();
           adpfloat_tmp.set_value(decoder[16*i + j], adpbias_dec);
           decoder_vec[j] =adpfloat_tmp.to_rawbits();
           adpfloat_tmp.Reset();
         } // for j
      rva_in_src.rw = 1;
      rva_in_src.data = decoder_vec.to_rawbits();
      rva_in_src.addr = 0x600000 + i*16;
      cout << "addresss_decoder: " << i << endl;
      rva_in.Push(rva_in_src);
      if (rva_in_src.addr ==  0x600000 + 15*16) {
         cout << "Pushed last decoder vector: " << endl;
      }
      wait();
    }  // for i
    wait();

    //send Attention start signal
    rva_in_src.rw = 1;
    rva_in_src.data = set_bytes<16>("00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00"); 
    rva_in_src.addr = set_bytes<3>("00_00_50");  
    rva_in.Push(rva_in_src);
    wait();
    wait(2340); 

    //rva_in_src.rw = 0;
    //rva_in_src.addr = 0x500000 + 16*16;
    //rva_in.Push(rva_in_src);
    

    //GBControl AXI GBControlConfig 
    rva_in_src.rw = 1;
    rva_in_src.data = set_bytes<16>("00_00_00_00_00_00_00_00_00_10_00_00_00_00_03_01"); //is_valid=1, mode=3, is_rnn=0, memory_index=0, output_memory_index=0, num_vector= 16, num_output_vector=0, num_timestep=320, num_timestep_padding=0, adpbias_act=2, adpbias_atten=0, adpbias_beta = 0, adpbias_dec=1
    rva_in_src.addr = set_bytes<3>("70_00_10");  // last 4 bits never used 
    rva_in.Push(rva_in_src);
    wait(); 

    //send GBControl start signal
    rva_in_src.rw = 1;
    rva_in_src.data = set_bytes<16>("00_00_00_00_00_00_00_00_00_00_00_00_00_00_00_00"); 
    rva_in_src.addr = set_bytes<3>("00_00_10");  
    rva_in.Push(rva_in_src);
    wait();
    
    const int NumVCol = 16;   
    const int NumVRow = 6;
    //std::vector<std::vector<float>> weight_ref_float(NumVRow*16, std::vector<float>(NumVCol*16, 0));
    std::vector<std::vector<double>> weight_ref_float_T;
    //std::vector<float>              input_ref_float(NumVCol*16, 0);    
    std::vector<double>              out1_ref_float;
    std::vector<double>              soft_ref_float;
    std::vector<double>              out2_ref_float;  

    weight_ref_float_T = TransposeMatrix(encoder_source);

    // First BMM1 output 
    // Generate Reference Output 
    out1_ref_float = MatrixVectorMul(encoder_source, decoder);
    cout << "Output1 Reference Value " << endl;    
    for (int i = 0; i < NumVRow*16; i++) { 
      cout << out1_ref_float[i] << endl;
    }
    
    // SoftMax
    soft_ref_float = SoftMax(out1_ref_float);
    cout << "SoftMax Reference Value " << endl;    
    for (int i = 0; i < NumVRow*16; i++) { 
      cout << soft_ref_float[i] << endl;
    }

    out2_ref_float = MatrixVectorMul(weight_ref_float_T, soft_ref_float);
    // Second BMM2 output 
    // TODO try to reformat the data for Dest l
    cout << "Output2 Reference Value " << endl;
    for (int i = 0; i < NumVCol; i++) { 
      for (int j = 0; j < 16; j++) {
        cout << out2_ref_float[16*i+j] << endl;
        //dest.output_ref_vec.push_back(out2_ref_float[16*i+j]);
      }
    }    
   
 
  } // attention_run()

}; //SC MODULE Source

SC_MODULE(Dest) {
  sc_in<bool> clk;
  sc_in<bool> rst;
  Connections::In<spec::Axi::SlaveToRVA::Read> rva_out;
  Connections::In<spec::StreamType> data_out; 
  Connections::In<bool> done; 
  Connections::In<bool> pe_start; 

  spec::Axi::SlaveToRVA::Read rva_out_dest;
  spec::StreamType data_out_dest;
  bool done_dest;
  bool pe_start_dest;

  SC_CTOR(Dest) {
    SC_THREAD(PopDataOut);
    sensitive << clk.pos();
    async_reset_signal_is(rst, false);
    SC_THREAD(PopDone);
    sensitive << clk.pos();
    async_reset_signal_is(rst, false);
    SC_THREAD(PopPEStart);
    sensitive << clk.pos();
    async_reset_signal_is(rst, false);
    SC_THREAD(RVA_Out);
    sensitive << clk.pos();
    async_reset_signal_is(rst, false);

  }   
  void RVA_Out() {
    wait();

    //unsigned i = 0;
    while (1) {
      if (rva_out.PopNB(rva_out_dest)) {
        cout << sc_time_stamp() << " Dest rva_out read data" << " \t " << endl;
        for (int i = 0; i < spec::kNumVectorLanes; i++) {
          AdpfloatType<8,3> tmp(rva_out_dest.data[i]);
          cout << tmp.to_float(0) << endl; //XXX check adativefloat bias value 
        }
      }
      wait(1);
    } //while
  } //RVA_Out

  void PopDataOut() {
   wait();
   while (1) {
     if (data_out.PopNB(data_out_dest)) {
        //cout << hex << sc_time_stamp() << " data_out data = " << data_out_dest.data << endl;
        cout << sc_time_stamp() << " Design data_out result" << " \t " << endl;
        for (int i = 0; i < spec::kNumVectorLanes; i++) {
          AdpfloatType<8,3> tmp(data_out_dest.data[i]);
          cout << tmp.to_float(0) << endl; //XXX check adativefloat bias value 
        }
     }
     wait(); 
   } // while
  } //PopDataOut

  void PopDone() {
   wait();
   while (1) {
     if (done.PopNB(done_dest)) {
        cout << sc_time_stamp() << " Done signal issued!!!" << " \t " << done_dest << endl;
     }
     wait(); 
   } // while
  } //PopDone

  void PopPEStart() {
   wait();
   while (1) {
     if (pe_start.PopNB(pe_start_dest)) {
        cout << sc_time_stamp() << " PE_start signal issued!!!" << " \t " << pe_start_dest << endl;
     }
     wait(); 
   } // while
  } //PopPEStart

}; //SC MODULE Dest

SC_MODULE(testbench) {
  SC_HAS_PROCESS(testbench);
  sc_clock clk;
  sc_signal<bool> rst;

  Connections::Combinational<spec::Axi::SlaveToRVA::Write> rva_in;
  Connections::Combinational<spec::Axi::SlaveToRVA::Read> rva_out;
  Connections::Combinational<spec::StreamType> data_in;     
  Connections::Combinational<spec::StreamType> data_out;     
  Connections::Combinational<bool> pe_start;  
  Connections::Combinational<bool> pe_done;  
  Connections::Combinational<bool> done;  

  NVHLS_DESIGN(GBModule) dut;
  Source  source;
  Dest    dest;

  testbench(sc_module_name name)
  : sc_module(name),
    clk("clk", 1.0, SC_NS, 0.5, 0, SC_NS, true),
    rst("rst"),
    dut("dut"),
    source("source"),   
    dest("dest")
  {

    dut.clk(clk);
    dut.rst(rst);
    dut.rva_in(rva_in);
    dut.rva_out(rva_out);
    dut.data_out(data_out);
    dut.data_in(data_in);
    dut.done(done);
    dut.pe_done(pe_done);
    dut.pe_start(pe_start);

    source.clk(clk);
    source.rst(rst);
    source.data_in(data_in);
    source.pe_done(pe_done);
    source.rva_in(rva_in);

    dest.clk(clk);
    dest.rst(rst);
    dest.rva_out(rva_out);
    dest.data_out(data_out);
    dest.pe_start(pe_start);
    dest.done(done);

    SC_THREAD(run); 
  }
  
  void run(){
    wait(2, SC_NS );
    std::cout << "@" << sc_time_stamp() <<" Asserting reset" << std::endl;
    rst.write(false);
    wait(2, SC_NS );
    rst.write(true);
    std::cout << "@" << sc_time_stamp() <<" De-Asserting reset" << std::endl;
    wait(100000, SC_NS );
    std::cout << "@" << sc_time_stamp() <<" sc_stop" << std::endl;
    sc_stop();
  }
}; //SC Module testbench

int sc_main(int argc, char *argv[]) {
  
  testbench tb("tb");
  sc_report_handler::set_actions(SC_ERROR, SC_DISPLAY);
  sc_start();

  bool rc = (sc_report_handler::get_count(SC_ERROR) > 0);
  if (rc)
    DCOUT("TESTBENCH FAIL" << endl);
  else
    DCOUT("TESTBENCH PASS" << endl);
  return rc;
}



using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using MultiWorldTesting;

namespace E4Lab
{
    class PolicyClass
    {
        
        public static UInt32 policyFunc(float threshold, CONTEXT context)
        {

            int score_begin = context.Features.Length - 4;
            float base_val = context.Features[score_begin].Value;
            uint num_action = 1;
            for (int i = 1; i < 4; i++)
            {
                if (context.Features[score_begin + i].Value >= base_val * threshold)
                {
                    num_action += (uint)Math.Pow(2, i - 1);
                }
            }
            return num_action;
        }
    }
}

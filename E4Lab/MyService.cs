using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using MultiWorldTesting;

namespace E4Lab
{
    class MyService
    {
        private const float epsilon = 0.1f;
        private const uint numActions = 8;        
        public MwtExplorer mwt;
        private uint requestCount;
        public MyService(MwtLogger logger, float defaultParam)
        {
            mwt = new MwtExplorer("myservice", logger);            
            mwt.InitializeEpsilonGreedy(epsilon, new StatefulPolicyDelegate<float>(PolicyClass.policyFunc), defaultParam, numActions);
            requestCount = 0;
        }

        public Tuple<uint, uint> ProcessRequest(CONTEXT context)
        {
            uint unique_id = requestCount++;
            uint action = mwt.ChooseAction(unique_id.ToString(), context);
            return new Tuple<uint, uint>(action, unique_id);
        }
    }
}

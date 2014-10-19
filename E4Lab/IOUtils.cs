using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using MultiWorldTesting;

namespace E4Lab
{
    class IOUtils
    {
        public string contextfile, rewardfile;
        public List<CONTEXT> contexts;
        public List<float[]> rewards;
        private const int numActions = 8;
        private static int cur_id = 0;
        public IOUtils(string cfile, string rfile)
        {
            contextfile = cfile;
            rewardfile = rfile;
            contexts = new List<CONTEXT>();
            rewards = new List<float[]>();
        }

        public void ParseContexts()
        {
            Console.WriteLine("Parsing contexts");
            CONTEXT c;
            using (StreamReader sr = new StreamReader(contextfile))
            {
                String line;
                while ((line = sr.ReadLine()) != null)
                {
                    //Console.WriteLine(line);
                    char[] delims = { ' ', '\t' };
                    List<FEATURE> featureList = new List<FEATURE>();
                    string[] features = line.Split(delims);
                    foreach (string s in features)
                    {
                        char[] feat_delim = { ':' };
                        string[] words = s.Split(feat_delim);
                        //Console.Write("{0} ", words.Length);
                        if (words.Length >= 1 && words[0] != "")
                        {
                            FEATURE f = new FEATURE();
                            //Console.WriteLine("{0}", words[0]);
                            f.Id = UInt32.Parse(words[0]);
                            if (words.Length == 2)
                                f.Value = float.Parse(words[1]);
                            else
                                f.Value = (float)1.0;
                            featureList.Add(f);

                            if (f.Id == 32766 || f.Id == 0)
                            {
                                Console.WriteLine("Bad feature found in reading");
                            }
                        }
                    }
                    c = new CONTEXT(featureList.ToArray(), null);
                    contexts.Add(c);

                }
            }

            Console.WriteLine("Read {0} contexts", contexts.Count);
        }

        public void ParseRewards()
        {
            Console.WriteLine("Parsing rewards");
            using (StreamReader sr = new StreamReader(rewardfile))
            {
                String line;

                while ((line = sr.ReadLine()) != null)
                {
                    //Console.WriteLine(line);
                    char[] delims = { '\t' };
                    float[] reward_arr = new float[numActions];
                    string[] reward_strings = line.Split(delims);
                    int i = 0;
                    foreach (string s in reward_strings)
                    {
                        reward_arr[i++] = float.Parse(s);
                        if (i == numActions) break;
                    }
                    rewards.Add(reward_arr);
                }
            }
        }

        public CONTEXT getContext()
        {
            if (contexts.Count == 0)
                ParseContexts();

            if (cur_id < contexts.Count)
                return contexts[cur_id++];
            else
                return null;
        }

        public float getReward(uint action, uint uid)
        {
            if (rewards.Count == 0)
                ParseRewards();

            //Console.WriteLine("Read {0} rewards, uid = {1}, action = {2}", rewards.Count, uid, action);

            if (uid >= rewards.Count)
                Console.WriteLine("Found illegal uid {0}", uid);

            return rewards[(int)(uid)][action - 1];
        }

    }
}

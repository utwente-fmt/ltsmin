-- this is an experiment in grouping
-- regroup2: keep track of the permutations...

module Main where

import List
import IO
import System
import Debug.Trace
import Array
import Monad

-----------------------------------------------------------------------

addIndex x 0 l = x:l
addIndex x i (y:l) = y:addIndex x (i-1) l

delIndex 0 l = l
delIndex i (x:l) = let (y:k) = delIndex (i-1) l in (y:x:k)

swap_row 0 j (x:l) = addIndex x j l
swap_row i 0    l  = delIndex i l
swap_row i j (x:l) = x:swap_row (i-1) (j-1) l

swap g i j = map (swap_row i j) g
cost g = sum (map cost_row g)

fst3 (x,_,_) = x
snd3 (_,y,_) = y
trd3 (_,_,z) = z

cost_row l =  cost_row1 l

cost_row1 (('-',_,_):l) = cost_row1 l
cost_row1 (('+',_,c):l) = length c + cost_row2 l
cost_row1 _ = 0

cost_row2 (('-',_,c):l) = let m = cost_row2 l in if m==0 then 0 else m+length c
cost_row2 (('+',_,c):l) = length c + cost_row2 l
cost_row2 _ = 0
 
allswaps l = (0,0,cost l) : 
  [ (i,j,k) | 
      let n=length (head l) - 1,
      i <- [0..n], 
      j <- [0..n], 
      j<i || j>i+1,
      let k = cost (swap l i j)]

lesscost (a,b,c) (d,e,f) = compare c f

-- this doesn't take the _best_ swap, but the first that leads to a decrease.
-- drp c (_,_,k) = c<=k
-- firstswap g = head (dropWhile (drp (cost g)) (allswaps g) ++ [(0,0,cost g)])

bestswap g = minimumBy lesscost (allswaps g)

nextswap g = let (i,j,_) = bestswap g in  
                trace (show i ++ " -> " ++ show j ++ "  (" ++ show (cost g) ++ ")")
                 swap g i j

optimal_group g =
  let h = nextswap g in
  if cost g==cost h then g else optimal_group h

-----------------------------------------------------------------------

-- cost in transposed matrix

cost_t ([]:_) = 0
cost_t g     = let (heads,tails) = unzip $ map (\(x:l)->(x,l)) g
               in cost_row heads + cost_t tails

allinsert x [] = [[x]]
allinsert x (y:l) = (x:y:l) : map (y:) (allinsert x l)

allperms [] = [[]]
allperms (x:l) = concat (map (allinsert x) (allperms l))

lesscostperm (_,c) (_,f) = compare c f

bestresult g =
  let gs = allperms (transpose g)
      (h,_) = minimumBy lesscostperm $ map (\g->(g,cost_t g)) gs
  in transpose h

-----------------------------------------------------------------------

cmp (a,_,_) (b,_,_) = compare a b
cmp_row [] [] = EQ
cmp_row (x:l) (y:k) =
   case cmp x y of
     GT -> GT
     LT -> LT
     EQ -> cmp_row l k

sort_matrix g = sortBy cmp_row g

merge_rows [] [] = []
merge_rows ((a,b,c):l) ((d,e,f):k) = (a,b++e,c) : merge_rows l k

nub_matrix [] = []
nub_matrix [l] = [l]
nub_matrix (k:l:x) = 
  case cmp_row k l of
    EQ -> nub_matrix (merge_rows k l : x)
    _  -> k:nub_matrix (l:x)

-----------------------------------------------------------------

map2 _ [] = []
map2 n (x:l) = (n,x): map2 (n+1) l

mapping l = map2 0 l

state_mapping g = mapping (map trd3 (head g))
trans_mapping g = mapping (map (snd3 . head) g)

dep3 _ [] = []
dep3 c (('-',_,_):l) = dep3 (c+1) l
dep3 c (('+',_,_):l) = c : dep3 (c+1) l

dep2 r [] = []
dep2 r (l:g) = (r,dep3 0 l) : dep2 (r+1) g

dependencies i = dep2 0 i

-----------------------------------------------------------------

print_row fp []    = hPutStr fp "\n"
print_row fp ((c,_,_):l) = do hPutStr fp [c] ; print_row fp l

pprint _ []    = return ()
pprint fp (l:g) = do print_row fp l; pprint fp g

convert r c [] = []
convert r c (x:l) = (x,[r],[c]):convert r (c+1) l

readmatrix r g = do               
  b <- isEOF
  if b then return g else do
    l <- getLine
    readmatrix (r+1) (convert r 0 l : g)

stats s g =
  hPutStr stderr (s ++ ": " 
     ++ show (length g) ++ " rows, " 
     ++ show (length (head g)) ++ " columns, penalty: " 
     ++ show (cost g) ++ "\n")

-----------------------------------------------------------------

subsumed [] [] = True
subsumed (_:l) (('+',_,_):k) = subsumed l k
subsumed (('-',_,_):l) (_:k) = subsumed l k
subsumed _ _ = False

subsume [] result = result
subsume (l:g) result = subsume g (cover l result)

cover l [] = [l]
cover l (x:g) = 
  if subsumed l x then merge_rows x l : g
    else if subsumed x l then cover (merge_rows l x) g
    else x:cover l g

-----------------------------------------------------------------
colnub g = transpose_matrix $ nub_matrix $ transpose_matrix g
colunnub g = map rowunnub g

rowunnub [] = []
rowunnub ( (c,x,[y]):l )   = (c,x,[y]) : rowunnub l
rowunnub ( (c,x,y:z:k):l ) = (c,x,[y]) : rowunnub ( (c,x,z:k):l )

transelt (c,x,y) = (c,y,x)

transpose_matrix g = 
  map (map transelt) $ transpose g

main = do 
  l <- getArgs
  g <- readmatrix 0 []

  preprocess <- if (elem "-subsume" l) then
    return (\g -> sort_matrix $ subsume g [])
   else
    return (\g -> nub_matrix $ sort_matrix g)

  optimize <- if (elem "-sort" l) then
    return (\g -> g)
   else if (elem "-best" l) then
    return (\g -> sort_matrix $ bestresult g)
   else
    return (\g -> sort_matrix $ optimal_group g)

  postprocess <- if (elem "-columns" l) then
    return (\g -> colunnub $ optimize $ colnub g)
   else
    return (\g -> optimize g)

  let h = preprocess g
  let i = postprocess h  

  i `seq`                   -- to force the trace output before the stats...
   stats "Input  matrix" g
  stats  "Sorted matrix" (if elem "-columns" l then colnub h else h)
  stats  "Output matrix" i

  if (elem "-matrix" l)
   then do 
     hPutStr stderr "Resulting Matrix:\n"
     pprint stdout i
   else do
     hPutStr stderr "Writing state mapping, transition mapping, dependencies to stdout:\n"
     print (state_mapping i)
     print (trans_mapping i)
     print (dependencies i)
